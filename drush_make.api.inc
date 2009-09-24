<?php
// $Id$

/**
 * The base project class.
 */
class DrushMakeProject {
  /**
   * Set attributes and retrieve project information.
   */
  protected function __construct($project) {
    foreach ($project as $key => $value) {
      $this->{$key} = $value;
    }
    $this->downloadClass = DrushMakeDownload::factory($this);
    if (!$this->downloadClass) {
      drush_error(dt('Could not find download type @type', array('@type' => $this->download['type'])));
    }
    $this->path = $this->generatePath();
  }

  public static function factory($project) {
    $project += array(
      'version' => DRUSH_MAKE_VERSION_BEST,
      'location' => DRUSH_MAKE_UPDATE_DEFAULT_URL,
      'subdir' => '',
      'directory_name' => '',
    );
    if (empty($project['type'])) {
      if (!($data = self::downloadData($project))) {
        return FALSE;
      }
      $project += $data;
    }
    if (!empty($project['type'])) {
      include_once 'drush_make.project.' . $project['type'] . '.inc';
      $class = 'DrushMakeProject_' . $project['type'];
      return new $class($project);
    }
    return FALSE;
  }

  /**
   * Build a project.
   */
  function make() {
    if (empty($this->made)) {
      if ($directory = $this->downloadClass->download()) {
        $this->fillPath($this->path);
        // Check is the directory is not there, perhaps the module already exits.
        if (!is_dir($this->path .'/'. $directory)) {
          $directory = rtrim($directory, '/');
          $path = $this->move($directory);
          $this->applyPatches($path);
        }
        else {
          drush_set_error(dt('Directory not empty: '). $directory);
          drush_shell_exec('rm -r ' . $directory);
        }
        // Prevent making the same project multiple times.
        $this->made = TRUE;
      }
      else {
        drush_set_error(dt('Download failed'));
      }
    }
  }

  /**
   * Retrieve and apply any patches specified by the makefile to this project.
   */
  protected function applyPatches($project_directory) {
    if (!empty($this->patch)) {
      foreach ($this->patch as $url) {
        // Download the patch.
        list($filename) = array_reverse(explode('/', $url));
        if (!drush_shell_exec("wget {$url}")) {
          drush_shell_exec("curl -O {$url}");
        }
        if (file_exists($filename)) {
          drush_shell_exec("mv {$filename} {$project_directory}");
          drush_shell_exec("patch -p0 -d {$project_directory} < {$project_directory}/{$filename}");
          drush_print("{$this->name} patched with {$filename}.");
        }
        else {
          drush_set_error("Unable to download $filename from $url.");
        }
      }
    }
  }

  /**
   * Move a project to its final resting place.
   */
  protected function move($project_directory) {
    drush_shell_exec("mv {$project_directory} {$this->path}");
    return "{$this->path}/{$project_directory}";
  }

  /**
   * Generate the proper path for this project type.
   */
  protected function generatePath() {
    $path = $this->base_path;
    // Generate subdirectory paths.
    if (!empty($this->project_path)) {
      $path .= '/' . $this->project_path;
    }
    if (!empty($this->subdir)) {
      $path .= '/' . $this->subdir;
    }
    if ($this->directory_name) {
      $this->path .= '/' . $this->directory_name;
    }
    return $path;
  }

  protected function fillPath($path) {
    $built_path = array();
    foreach (explode('/', $path) as $part) {
      $built_path[] = $part;
      $current_path = implode('/', $built_path);
      if (!is_dir($current_path)) {
        drush_op('mkdir', $current_path);
      }
    }
  }

  protected static function downloadData($project) {
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
}

class DrushMakeDownload {
  var $project;
  var $path;

  protected function __construct($project) {
    $this->project = $project;
    $this->path = !empty($this->project->directory_name) ? $this->project->directory_name : $this->project->name;
    $this->preDownload();
  }

  function factory($project) {
    if (empty($project->download) || empty($project->download['type'])) {
      $project->download = array('type' => 'updatexml');
    }
    include_once 'drush_make.download.' . $project->download['type'] . '.inc';
    $class = 'DrushMakeDownload_' . $project->download['type'];
    if (class_exists($class)) {
      return new $class($project);
    }
    else {
      return FALSE;
    }
  }

  function preDownload() {
    return FALSE;
  }

  function download() {
    return FALSE;
  }
}
