<?php
// $Id$

define('DRUSH_MAKE_UPDATE_DEFAULT_URL', 'http://updates.drupal.org/release-history');
define('DRUSH_MAKE_VERSION_BEST', 'best');

/**
 * Implementation of hook_drush_command().
 */
function drush_make_drush_command() {
  $items['make'] = array(
    'description' => 'Turns a makefile into a working drupal install.',
    'callback' => 'drush_make',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'examples' => array(
      'drush make foo.make.info' => 'Make the Drupal described in foo.make.info.',
    ),
    'arguments' => array(
      'path/to/script' => 'The path to the makefile. The path may be absolute or relative to the current working dir.',
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
 * Drush callback; make based on the makefile.
 */
function drush_make($makefile = NULL) {
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
  }
  elseif (empty($info['projects'])) {
    drush_set_error(dt('The specified makefile doesn\'t have any projects.'));
  }
  elseif (empty($info['core'])) {
    drush_set_error(dt('The specified makefile doesn\'t have a core version.'));
  }

  drush_print(dt('Getting project information...'));

  // The final array of projects.
  $projects = array();

  // As a first pass, get project information..
  foreach ($info['projects'] as $project => $project_info) {
    // Cover if there is no project info, it's just a project name.
    if (is_string($project_info) && is_numeric($project)) {
      $project = $project_info;
      $project_info = array();
    }

    // Add some defaults.
    $project_info += array(
      'name' => $project,
      'version' => DRUSH_MAKE_VERSION_BEST,
      'location' => DRUSH_MAKE_UPDATE_DEFAULT_URL,
      'subdir' => '',
      'directory_name' => '',
    );

    drush_get_project_info($project_info, $info['core']);

    $projects[] = $project_info;
  }

  $drupal_filename = drush_make_download_drupal($info);

  drush_print(dt('Downloading projects...'));
  foreach ($projects as $project) {
    // Assume we're in a Drupal root at this point.
    $path = 'sites/all/' . $project['type'] . 's';
    // If sites/all/[project type] doesn't exist, create it.
    if (!is_dir($path)) {
      drush_op('mkdir', $path);
    }

    if (!empty($project['subdir'])) {
      foreach (explode('/', $project['subdir']) as $part) {
        if (!is_dir($part)) {
          drush_op('mkdir', $part);
        }
        $path .= '/' . $part;
      }
    }

    if (!$file = drush_make_download_project($project)) {
      continue;
    }

    // Get the filename once unzipped.
    drush_shell_exec('tar -tf ' . $file);
    list($project_directory) = drush_shell_exec_output();
    drush_shell_exec('tar -xf ' . $file);
    drush_op('unlink', $file);

    if ($project['directory_name']) {
      $path .= '/' . $project['directory_name'];
    }
    else {
      $path .= '/' . trim($project_directory, '/');
    }
    drush_shell_exec('mv ' . $project_directory . ' ' .  $path);
  }
    
  if (drush_get_option('copy-settings')) {
    drush_shell_exec("cp sites/default/default.settings.php sites/default/settings.php");
  }

  if ($drupal_filename) {
    // Tarring is enabled.
    drush_op('chdir', '..');
    drush_shell_exec("tar -czf $drupal_filename.tar.gz $drupal_filename");
    drush_shell_exec('rm -r ' . $drupal_filename);
  }
}

/**
 * Find the best release for the given major and the set of releases.
 */
function drush_make_find_best_release($major, $releases) {
  $best = NULL;
  $best_release = NULL;
  foreach ($releases as $release => $release_info) {
    $version = $release_info['patch'] . $release_info['extra'];
    if ($release_info['major'] == $major && version_compare($best, $version, '<')) {
      $best = $version;
      $best_release = $release;
    }
  }
  return $best_release;
}

function drush_get_project_info(&$project_info, $core) {
  // First, get the release history.
  @$release_history = simplexml_load_file($project_info['location'] . '/' . $project_info['name'] . '/' . $core);
  if (!$release_history) {
    drush_set_error(dt("Could not retrieve version information for @project.", array('@project' => $project)));
    return;
  }

  // Determine the project type.
  $term_map = array('Modules' => 'module', 'Themes' => 'theme');
  // Iterate through all terms related to this project.
  foreach ($release_history->terms->term as $term) {
    // If the term is eather 'Modules' or 'Themes', add it.
    if (in_array((string) $term->value, array_keys($term_map))) {
      $project_info['type'] = $term_map[(string)$term->value];
      break;
    }
  }

  // Make an array of releases.
  $releases = array();
  foreach ($release_history->releases->release as $release) {
    $version = (string) $release->version_major;
    if ((string) $release->version_patch === '0' || (string) $release->version_patch) {
      $version .= '.' . (string) $release->version_patch;
    }
    if ($extra_version = (string) $release->version_extra) {
      $version .= '-' . $extra_version;
    }
    $releases[$version] = array(
      'file' => (string) $release->download_link,
      'md5' => (string) $release->mdhash,
    );
    foreach (array('major', 'patch', 'extra') as $part) {
      $releases[$version][$part] = (string) $release->{'version_' . $part};
    }
  }

  // Find the best release.
  if ($project_info['version'] == DRUSH_MAKE_VERSION_BEST) {
    $recommended_major = (string)$release_history->recommended_major;
    $project_info['version'] = drush_make_find_best_release($recommended_major, $releases);
  }
  else {
    $exploded_version = explode('.', $project_info['version']);
    if (count($exploded_version) == 1) {
      $project_info['version'] = drush_make_find_best_release($exploded_version, $releases);
    }
  }
  $project_info['release'] = $releases[$project_info['version']];
}

function drush_make_download_project($project) {
  list($filename) = array_reverse(explode('/', $project['release']['file']));
  // Download the project.
  if (!drush_shell_exec("wget " . $project['release']['file'])) {
    drush_shell_exec("curl -O " . $project['release']['file']);
  }
  if (file_exists($filename)) {
    drush_print("{$project['name']}-{$project['version']} downloaded.");
  }
  else {
    drush_set_error("Unable to download $filename from " . $project['release']['file']);
    return;
  }

  // Check Md5 hash.
  if (md5_file($filename) != $project['release']['md5']) {
    drush_op('unlink', $filename);
    drush_set_error("Error: File $filename is corrupt (wrong md5 checksum).");
    return;
  }

  // Unzip the file.
  drush_shell_exec('gzip --list ' . $filename);
  $gzinfo = drush_shell_exec_output();

  if (!count($gzinfo)) {
    drush_set_error(dt('Could not retrieve package information for @filename.', array('@filename' => $filename)));
    return;
  }

  // Unzip it and then delete the tar file.
  drush_shell_exec('gzip -d ' . $filename);

  $gzinfo = array_pop($gzinfo);
  $matches = array();
  preg_match('/[a-zA-Z0-9.\-_,]*.tar/', $gzinfo, $matches);
  if (!isset($matches[0])) {
    drush_set_error(dt('Could not retrieve tar filename from @filename for @project.', array('@project' => $project, '@filename' => $filename)));
    return;
  }

  return $matches[0];
}

function drush_make_download_drupal($info) {
  $project_info = array(
    'name' => 'drupal',
    'version' => DRUSH_MAKE_VERSION_BEST,
    'location' => DRUSH_MAKE_UPDATE_DEFAULT_URL,
  );
  drush_get_project_info($project_info, $info['core']);
  $file = drush_make_download_project($project_info);

  // Get the filename once unzipped.
  drush_shell_exec('tar -tf ' . $file);
  $output = drush_shell_exec_output();
  drush_shell_exec('tar -xf ' . $file);
  drush_op('unlink', $file);
  if ($tar = drush_get_option('tar')) {
  $file = trim(array_shift($output), '/');
  drush_shell_exec('mv ' . $file . ' ' . $tar);
  chdir($tar);
  return $tar;
  }
  else {
    foreach ($output as $item) {
      $parts = explode('/', trim($item, '/'));
      if (count($parts) == 2) {
        drush_shell_exec('mv ' . $item . ' ' . $parts[1]);
      }
      elseif (count($parts) == 1) {
        $root = $parts[0];
      }
    }
    drush_shell_exec('rm -r ' . $root);
  }
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