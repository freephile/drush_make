<?php

class DrushMakeProject_Theme extends DrushMakeProject {
  var $project_path = 'sites/all/themes';
  public function __construct(&$project) {
    parent::__construct($project);
  }
}
