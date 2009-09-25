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
  }

  /**
   * Build a project.
   */
  function make() {
    if (empty($this->made)) {
      if ($directory = $this->downloadClass->download()) {
        $this->path = $this->generatePath();
        $this->fillPath($this->path);
        // Check is the directory is not there, perhaps the module already exits.
        if (!is_dir($this->path .'/'. $directory)) {
          $directory = rtrim($directory, '/');
          $path = $this->move($directory);
          $this->applyPatches($path);

          // If the project itself containes any make file, build them too.
          $make_files = drush_scan_directory($path, '.make');
          $build_path = $this->generatePath(FALSE) ."/". $directory;
          foreach ($make_files as $file) {
            if ($info = drush_make_get_info($file->filename)) {
              drush_make_build($info, $this->base_path, $build_path);
            }
          }
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
   *
   * @param $base_path
   *   Whether to include the base_path in the generated patch. Defaults to
   *   true.
   */
  protected function generatePath($base = true) {
    $path = array();
    if ($base) {
      $path[] = $this->base_path;
    }
    if (!empty($this->project_path)) {
      $path[] = $this->project_path;
    }
    if (!empty($this->subdir)) {
      $path[] = $this->subdir;
    }
    return implode('/', $path);
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
