<?php
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
function drush_make($makefile) {
  // Set empty arrays for the settings, in case they aren't filled in.
  $projects = array();
  $extra = array();
  // Include the file itself.
  include_once $makefile;
  // Add default settings for the extra.
  $extra += array(
    'copy_settings' => FALSE,
    'tar' => FALSE,
  );
  // Find a major version.
  list($major) = explode('.', $projects['drupal']['version']);
  $major .= '.x';
  drush_print(dt('Downloading projects...'));
  foreach ($projects as $project => $info) {
    // The project can sometimes have no info.
    if (is_string($info) && is_numeric($project)) {
      $project = $info;
      $info = array();
    }
    // Add default info.
    $info += array(
      'version' => DRUSH_MAKE_VERSION_BEST,
      'location' => DRUSH_MAKE_UPDATE_DEFAULT_URL,
      'subdir' => '',
      'directory_name' => '',
    );
    $info['version'] = (string) $info['version'];
    // First, get the release history.
    @$xml = simplexml_load_file($info['location'] . '/' . $project . '/' . $major);
    if (!$xml) {
      drush_set_error('RELEASE_HISTORY', dt("Could not retrieve version information for @project.", array('@project' => $project)));
      continue;
    }
    // Determine the project type.
    $term_map = array('module' => 'Modules', 'theme' => 'Themes', 'drupal' => 'Drupal project', 'translation' => 'Translations');
    $flip_term_map = array_flip($term_map);
    foreach ($xml->terms->term as $term) {
      if (in_array((string) $term->value, $term_map)) {
        // Get the code-friendly name of the type.
        $type = $flip_term_map[(string)$term->value];
        break;
      }
    }
    // Make a list of all the releases, as well as some important information about them.
    $releases = array();
    foreach ($xml->releases->release as $release) {
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
    // If the version isn't specified, we'll choose the best version.
    if ($info['version'] == DRUSH_MAKE_VERSION_BEST) {
      // First, find the recommended major.
      $recommended_major = (string)$xml->recommended_major;
      $info['version'] = drush_make_find_best_release($recommended_major, $releases);
    }
    // If the version is specified and it's just a branch, the below will catch
    // it. Otherwise, there's enough info that it doesn't need to be processed.
    else {
      $exploded_version = explode('.', $info['version']);
      if (count($exploded_version) == 1) {
        $info['version'] = drush_make_find_best_release($exploded_version[0], $releases);
      }
    }
    // Put the release in the info array.
    $info['release'] = $releases[$info['version']];

    $filename = explode('/', $info['release']['file']);
    $filename = array_pop($filename);
    $untar_name = str_replace(array('.tar.gz', '.tgz'), '', $filename);
    if ($type == 'drupal') {
      $drupal_filename = $untar_name;
    }
    else {
      drush_op('chdir', $drupal_filename . '/sites/all');
      if (!is_dir($type . 's')) {
        drush_op('mkdir', $type . 's');
      }
      drush_op('chdir', $type . 's');
    }
    if (!empty($info['subdir'])) {
      $subdir = explode('/', $info['subdir']);
      foreach ($subdir as $part) {
        if (!is_dir($part)) {
          drush_op('mkdir', $part);
        }
        drush_op('chdir', $part);
      }
    }

    // Download it.
    if (!drush_shell_exec("wget " . $info['release']['file'])) {
      drush_shell_exec("curl -O " . $info['release']['file']);
    }

    // Check Md5 hash
    if (md5_file($filename) != $info['release']['md5']) {
      drush_op('unlink', $filename);
      drush_set_error("MD5_WRONG", "Error: File $filename is corrupt (wrong md5 checksum).");
      continue;
    }

    // Retrieve file info from the gzip archive. In particular, we need
    // to get the original filename so that we can untar it successfully.
    drush_shell_exec("gzip --list {$filename}");
    $gzinfo = drush_shell_exec_output();
    drush_shell_exec("gzip -d {$filename}");

    if (!count($gzinfo)) {
      drush_error(dt("Could not retrieve package information for @filename.", array('@filename' => $filename)));
      continue;
    }
    $gzinfo = array_pop($gzinfo);
    $matches = array();
    preg_match('/[a-zA-Z0-9.\-_,]*.tar/', $gzinfo, $matches);
    if (!isset($matches[0])) {
      drush_error(dt("Could not retrieve tar filename from @filename for @project.", array('@project' => $project, '@filename' => $filename)));
      continue;
    }
    $filename = $matches[0];

    drush_shell_exec("tar -xf " . $filename);
    if ($info['directory_name']) {
      drush_shell_exec('mv '. $untar_name . ' ' . $info['directory_name']);
      if ($type == 'drupal') {
        $drupal_filename = $info['directory_name'];
      }
    }

    // Remove the tarball
    drush_op('unlink', $filename);
    if (isset($subdir)) {
      drush_op('chdir', implode('/', array_fill(0, count($subdir), '..')));
    }
    if ($type != 'drupal') {
      drush_op('chdir', '../../../..');
    }
  }
  if ($extra['copy_settings']) {
    drush_shell_exec("cp $drupal_filename/sites/default/default.settings.php $drupal_filename/sites/default/settings.php");
  }
  if ($extra['tar']) {
    // Probably doesn't work on winbloze.
    drush_shell_exec("tar -czf $drupal_filename.tar.gz $drupal_filename");
    drush_shell_exec('rm -r ' . $drupal_filename);
  }
}

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
