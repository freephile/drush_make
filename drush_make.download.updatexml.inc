<?php
// $Id$

include_once 'drush_make.download.get.inc';

class DrushMakeDownload_UpdateXML extends DrushMakeDownload_Get {
  public function __construct(&$project) {
    parent::__construct($project);
  }

  function preDownload() {
    // Make an array of releases.
    foreach ($this->project->release_history->releases->release as $release) {
      $version = (string) $release->version_major;
      if ((string) $release->version_patch) {
        $version .= '.' . (string) $release->version_patch;
      }
      else {
        $version .= '.0';
      }
      if ($extra_version = (string) $release->version_extra) {
        $version .= '-' . $extra_version;
      }
      $this->releases[$version] = array(
        'file' => (string) $release->download_link,
        'md5' => (string) $release->mdhash,
        'version' => (string) $release->version,
      );
      foreach (array('major', 'patch', 'extra') as $part) {
        $this->releases[$version][$part] = (string) $release->{'version_' . $part};
      }
    }

    // Find the best release.
    if ($this->project->version == DRUSH_MAKE_VERSION_BEST) {
      $recommended_major = (string)$this->project->release_history->recommended_major;
      $this->project->version = $this->getBestRelease($recommended_major);
    }
    else {
      $exploded_version = explode('.', $this->project->version);
      if (count($exploded_version) == 1) {
        list($major) = $exploded_version;
        $this->project->version = $this->getBestRelease($major);
      }
      // Otherwise we have a full version.
    }
    $this->release = $this->releases[$this->project->version];
    $this->version = $this->release['version'];
    $this->project->download['url'] = $this->release['file'];
  }

  function download() {
    if (!($filename = $this->downloadFile())) {
      return;
    }

    // Check Md5 hash.
    if (md5_file($filename) != $this->release['md5']) {
      drush_op('unlink', $filename);
      drush_set_error("Error: File $filename is corrupt (wrong md5 checksum).");
      return;
    }

    return $this->unpack($filename);
  }

  protected function getBestRelease($major) {
    $best = NULL;
    foreach ($this->releases as $version => $release_info) {
      if ($release_info['major'] == $major && version_compare($best, $version, '<')) {
        $best = $version;
      }
    }
    return $best;
  }
}
