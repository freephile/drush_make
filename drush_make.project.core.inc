<?php
// $Id$

class DrushMakeProject_Core extends DrushMakeProject {
  public function __construct(&$project, $build_path) {
    parent::__construct($project);
  }
  protected function move($project_directory) {
    drush_shell_exec("mv {$project_directory}/* {$this->base_path}");
    drush_shell_exec("mv {$project_directory}/.* {$this->base_path}");
    drush_shell_exec("rmdir {$project_directory}");
  }
}
