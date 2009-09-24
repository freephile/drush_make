<?php
// $Id$

class DrushMakeProject_Module extends DrushMakeProject {
  public function __construct(&$project, $build_path) {
    $this->project_path = $build_path .'/modules';
    parent::__construct($project);
  }
}
