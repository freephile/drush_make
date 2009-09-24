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
function drush_make_get_base_path($destination = '', $reset = FALSE) {
  static $base_path;
  if (!isset($base_path) || $reset) {
    $drupal_root = drush_get_option(array('r', 'root'), drush_locate_root());
    if (drush_get_option('tar')) {
      $base_path = drush_get_option('tar');
    }
    else if (!empty($destination)) {
      $base_path = trim($destination, '/');
    }
    elseif (!empty($drupal_root) && drush_valid_drupal_root($drupal_root)) {
      $base_path = $drupal_root;
    }
    elseif(drush_confirm(dt("Make new site in the current directory?"))) {
      $base_path = '.';
    }
    else {
      drush_die(dt("Build aborted."));
    }
  }
  return $base_path;
}

/**
 * Drush callback; make based on the makefile.
 */
function drush_make($makefile = NULL, $base_path = NULL) {
  if (!($info = drush_make_get_info($makefile))) {
    return;
  }

  drush_print(dt('Getting project information...'));
  $base_path = drush_make_get_base_path($base_path);

  if (!empty($info['destination'])) {
    $path = $base_path . '/_drush_make_tmp';
    drush_op('mkdir', $path);
    drush_shell_exec('ls');
    $files = drush_shell_exec_output();
    foreach ($files as $file) {
      drush_shell_exec("mv ./{$file} {$path}/{$file}");
    }
  }

  // Retrieve project information
  $projects = array();

  foreach ($info['projects'] as $project => $project_info) {
    // Cover if there is no project info, it's just a project name.
    if (is_string($project_info) && is_numeric($project)) {
      $project = $project_info;
      $project_info = array();
    }
    $project_info += array(
      'name'      => $project,
      'core'      => $info['core'],
      'base_path' => $base_path,
    );

    $project = $projects[$project_info['name']] = DrushMakeProject::factory($project_info);
    if ($project->type == 'core') {
      unset($projects[$project->name]);
      $core = $project;
    }
  }
  if (!isset($core)) {
    $core = DrushMakeProject::factory(array(
      'name' => 'drupal',
      'core' => $info['core'],
      'base_path' => $base_path,
    ));
  }

  // Ensure no errors occured before starting to build. In particular this will
  // catch situations where the update xml could not be retrieved.
  if (drush_get_error()) {
    drush_die();
  }

  drush_print(dt('Building projects...'));

  $drupal_root = drush_get_option(array('r', 'root'), drush_locate_root());
  if (empty($drupal_root) || !drush_valid_drupal_root($drupal_root)) {
    $core->make();
  }
  // Build projects
  foreach ($projects as $project) {
    $project->make();
  }

  // Copy settings.php.
  if (drush_get_option('copy-settings')) {
    drush_shell_exec("cp {$base_path}/sites/default/default.settings.php {$base_path}/sites/default/settings.php");
  }


  if (!empty($info['destination'])) {
    $path = $base_path . '/' . $info['destination'];
    if (!is_dir($path)) {
      drush_op('mkdir', $path);
    }
    foreach ($files as $file) {
      drush_shell_exec("mv {$base_path}/_drush_make_tmp/{$file} {$path}/{$file}");
    }
    drush_op('rmdir', $base_path . '/_drush_make_tmp');
  }

  // Tarring is enabled.
  if (drush_get_option('tar')) {
    drush_shell_exec("tar -czf $base_path.tar.gz $base_path");
    drush_shell_exec('rm -r ' . $base_path);
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
