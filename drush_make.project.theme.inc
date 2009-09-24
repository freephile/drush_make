<?php
// $Id$

class DrushMakeProject_Theme extends DrushMakeProject {
  public function __construct(&$project, $build_path) {
    $this->project_path = $build_path .'/themes';
    parent::__construct($project);
  }
}
