<?php
// $Id$

class DrushMakeProject_Profile extends DrushMakeProject {
  public function __construct(&$project, $build_path) {
    $this->project_path = 'profiles';
    parent::__construct($project);
  }
}
