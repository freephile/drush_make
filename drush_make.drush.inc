<?php
// $Id$

define('DRUSH_MAKE_UPDATE_DEFAULT_URL', 'http://updates.drupal.org/release-history');
define('DRUSH_MAKE_VERSION_BEST', 'best');

include_once 'drush_make.api.inc';

/**
 * Implementation of hook_drush_command().
 */
function drush_make_drush_command() {
  $items['make'] = array(
    'description' => 'Turns a makefile into a working drupal install.',
    'callback' => 'drush_make',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'examples' => array(
      'drush make [script] [directory]' => 'Build the Drupal site described in [script] to [directory].',
      'drush make foo.make bar' => 'Builds foo.make into a Drupal site in the bar/ directory.',
    ),
    'arguments' => array(
      'script' => 'The path to the makefile. The path may be absolute or relative to the current working dir.',
      'directory' => 'The optional destination directory for the build. If omitted, the site will be built to the current working dir.',
    ),
    'options' => array(
      '--copy-settings' => 'Copy default.settings.php to settings.php in sites/default upon completion.',
      '--tar=[file]' => 'Generate an archive of the build at [file].tar.gz instead of to a directory.',
      '--working-copy' => 'Check out working copies of any projects that specify version control.',
    ),
  );
 
  return $items;
}

/**
 * Implementation of hook_drush_help().
 */
function drush_make_drush_help($section) {
  switch ($section) {
    case 'drush:make':
      return dt("@TODO: Print information on the format of the makefile here.");
  }
}

/**
 * Establish the destination base_path for this build.
 */
function drush_make_get_base_path($destination = '') {
  if (drush_get_option('tar')) {
    $base_path = drush_get_option('tar');
  }
  elseif (!empty($destination)) {
    $base_path = trim($destination, '/');
  }
  elseif(drush_confirm(dt("Make new site in the current directory?"))) {
    $base_path = '.';
  }
  else {
    drush_print(dt('Build aborted.'));
    return FALSE;
  }
  return $base_path;
}

/**
 * Drush callback; make based on the makefile.
 */
function drush_make($makefile = NULL, $base_path = NULL) {
  $temp_dir = 'tmp'. time();
  if (file_exists($temp_dir)) {
    drush_set_error("DRUSH_MAKE_ERROR", dt("Couldn't make temporary directory."));
    return;
  }

  // Start the build.
  if ($info = drush_make_get_info($makefile)) {
    if ($base_path = drush_make_get_base_path($base_path)) {
      $temp_base = "{$temp_dir}/{$base_path}";
      drush_make_build($info, $temp_base);

      // Copy settings.php.
      if (drush_get_option('copy-settings')) {
        drush_shell_exec("cp {$temp_base}/sites/default/default.settings.php {$temp_base}/sites/default/settings.php");
      }

      // Tarring is enabled.
      if (drush_get_option('tar')) {
        drush_shell_exec("tar -Pcf -  --transform='s,{$temp_base},{$base_path},' {$temp_base} | gzip > {$base_path}.tar.gz");
      }

      if ($base_path == '.') {
        drush_shell_exec("mv {$temp_base}/* .");
        drush_shell_exec("mv {$temp_base}/.* .");
      }
      elseif (file_exists($base_path)) {
        drush_set_error("DRUSH_MAKE_ERROR", dt("Could not move build into place. Directory already exists: ". $base_path));
        return;
      }
      else {
        drush_shell_exec("mv $temp_base $base_path");
      }
      drush_shell_exec("rm -r $temp_dir");
    }
  }
}

/**
 * Build a platform based on a make file.
 *
 * @param $info
 *   $info array as returned by drush_make_get_info()
 * @param $base_path
 *   The path where the platform will be assembled
 * @param $build_path
 *   The path where this set of packages be assembled.
 */
function drush_make_build($info, $base_path, $build_path = 'sites/all') {

  // Retrieve project information
  $projects = array();
  foreach ($info['projects'] as $project => $project_info) {
    // Cover if there is no project info, it's just a project name.
    if (is_string($project_info) && is_numeric($project)) {
      $project = $project_info;
      $project_info = array();
    }
    // Merge the known data onto the project info.
    $project_info += array(
      'name'      => $project,
      'core'      => $info['core'],
      'base_path' => $base_path,
    );

    $project = $projects[$project_info['name']] = drush_make_project_factory($project_info, $build_path);
    if ($project->type == 'core') {
      unset($projects[$project->name]);
      $core = $project;
    }
  }

  // Ensure no errors occured before starting to build. In particular this will
  // catch situations where the update xml could not be retrieved.
  if (drush_get_error()) {
    return;
  }

  // Core is a special case, we need to set it up first thing.
  if (isset($core)) {
    $core->make();
  }
  // Build projects
  foreach ($projects as $project) {
    $project->make();
  }
}

/**
 * Project object factory.
 *
 * @param $project
 *   The project info array.
 *
 * @return a DrushMakeProject_TYPE object.
 */
function drush_make_project_factory($project, $build_path) {
  $project += array(
    'version' => DRUSH_MAKE_VERSION_BEST,
    'location' => DRUSH_MAKE_UPDATE_DEFAULT_URL,
    'subdir' => '',
  );
  if (empty($project['type'])) {
    if (!($data = drush_make_download_data($project))) {
      return FALSE;
    }
    $project += $data;
  }
  if (!empty($project['type'])) {
    include_once 'drush_make.project.' . $project['type'] . '.inc';
    $class = 'DrushMakeProject_' . $project['type'];
    return new $class($project, $build_path);
  }
  return FALSE;
}

/**
 * Retrieve and process update xml.
 *
 * @param $project
 *   The project info array.
 *
 * @return
 *   Array of 'release_histroy' and 'type' info that can be added to the
 *   project info array.
 */
function drush_make_download_data($project) {
  // First, get the release history.
  @$release_history = simplexml_load_file($project['location'] . '/' . $project['name'] . '/' . $project['core']);
  if (!$release_history) {
    drush_set_error(dt("Could not retrieve version information for @project.", array('@project' => $project['name'])));
    return;
  }

  // Determine the project type.
  $term_map = array('Modules' => 'module', 'Themes' => 'theme', 'Drupal project' => 'core', 'Installation profiles' => 'profile');
  // Iterate through all terms related to this project.
  foreach ($release_history->terms->term as $term) {
    // If the term is eather 'Modules' or 'Themes', add it.
    if (in_array((string) $term->value, array_keys($term_map))) {
      return array('release_history' => $release_history, 'type' => $term_map[(string)$term->value]);
    }
  }
}

/**
 * Retrieve and parse the makefile.
 */
function drush_make_get_info($makefile) {
  // Do some error checking.
  if (!isset($makefile)) {
    drush_set_error(dt('Please specify a makefile.'));
    return;
  }
  elseif (!file_exists($makefile)) {
    drush_set_error(dt('The specified makefile doesn\'t exist.'));
    return;
  }
  elseif (!($info = drush_make_parse_info_file($makefile))) {
    drush_set_error(dt('The specified makefile doesn\'t exist.'));
    return;
  }
  elseif (empty($info['projects'])) {
    drush_set_error(dt('The specified makefile doesn\'t have any projects.'));
    return;
  }
  elseif (empty($info['core'])) {
    drush_set_error(dt('The specified makefile doesn\'t have a core version.'));
    return;
  }
  return $info;
}

/**
 * Parse Drupal info file format.
 *
 * Copied verbatim from includes/common.inc
 *
 * @see drupal_parse_info_file
 */
function drush_make_parse_info_file($filename) {
  $info = array();

  if (!file_exists($filename)) {
    return $info;
  }

  $data = file_get_contents($filename);
  if (preg_match_all('
    @^\s*                           # Start at the beginning of a line, ignoring leading whitespace
    ((?:
      [^=;\[\]]|                    # Key names cannot contain equal signs, semi-colons or square brackets,
      \[[^\[\]]*\]                  # unless they are balanced and not nested
    )+?)
    \s*=\s*                         # Key/value pairs are separated by equal signs (ignoring white-space)
    (?:
      ("(?:[^"]|(?<=\\\\)")*")|     # Double-quoted string, which may contain slash-escaped quotes/slashes
      (\'(?:[^\']|(?<=\\\\)\')*\')| # Single-quoted string, which may contain slash-escaped quotes/slashes
      ([^\r\n]*?)                   # Non-quoted string
    )\s*$                           # Stop at the next end of a line, ignoring trailing whitespace
    @msx', $data, $matches, PREG_SET_ORDER)) {
    foreach ($matches as $match) {
      // Fetch the key and value string
      $i = 0;
      foreach (array('key', 'value1', 'value2', 'value3') as $var) {
        $$var = isset($match[++$i]) ? $match[$i] : '';
      }
      $value = stripslashes(substr($value1, 1, -1)) . stripslashes(substr($value2, 1, -1)) . $value3;

      // Parse array syntax
      $keys = preg_split('/\]?\[/', rtrim($key, ']'));
      $last = array_pop($keys);
      $parent = &$info;

      // Create nested arrays
      foreach ($keys as $key) {
        if ($key == '') {
          $key = count($parent);
        }
        if (!isset($parent[$key]) || !is_array($parent[$key])) {
          $parent[$key] = array();
        }
        $parent = &$parent[$key];
      }

      // Handle PHP constants
      if (defined($value)) {
        $value = constant($value);
      }

      // Insert actual value
      if ($last == '') {
        $last = count($parent);
      }
      $parent[$last] = $value;
    }
  }

  return $info;
}
