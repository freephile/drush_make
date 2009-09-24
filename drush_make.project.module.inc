<?php
// $Id$

class DrushMakeProject_Module extends DrushMakeProject {
  var $project_path = 'sites/all/modules';
  public function __construct(&$project) {
    parent::__construct($project);
  }
}
