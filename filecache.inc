<?php

class DrupalFileCache implements DrupalCacheInterface {

  function __construct($bin) {
    $this->bin = $bin;
    $filecache_directory = variable_get('filecache_directory', FALSE);
    if ($filecache_directory === FALSE) {
      // Default directory, protected by Apache
      if (strstr($_SERVER['SERVER_SOFTWARE'], 'Apache') !== FALSE) {
	$directory = DRUPAL_ROOT .'/'. conf_path() . '/filecache';
	if (is_dir($directory) || mkdir($directory)) { // A bit of cleverness
	  $htaccess = $directory . '/.htaccess';
	  if (is_file($htaccess) ||
	      file_put_contents($htaccess, "Order allow,deny\n") !== FALSE) {
	    // Sucess in setting up protected filecache directory
	    $filecache_directory = $directory;
	  }
	}
      }
      if ($filecache_directory === FALSE) {
	// This is serious
	die('No filecache_directory and unability to setup default one.');
      }
    }
    //@todo Handle when filecache_directory is not set
    //@todo Support custom prefix
    $this->prefix = $filecache_directory .'/'. $bin .'-';
  }

  function get($cid) {
    $filename = $this->prefix . md5($cid);

    // Use @ because cache entry may not exist
    $content = @file_get_contents($filename);
    if ($content === FALSE) {
      return FALSE;
    }

    $cache = unserialize($content);
    if ($cache === FALSE) {
      unlink($filename); // Remove broken cache entries
      //dpm('Removed broken '.$bin.' '.$cid); 
      return FALSE;
    }

    // XXX Should reproduce the cache_lifetime / cache_flush_$bin logic
    $cache_flush = variable_get('filecache_flush_'. $this->bin, 0);
    if ($cache->expire != CACHE_TEMPORARY && // XXX how to handle this?
	$cache->expire != CACHE_PERMANENT &&
	($cache->expire < REQUEST_TIME ||
	 ($cache_flush && $cache->created < $cache_flush))) {
      // XXX For now this is the only place where cache entries are removed
      unlink($filename);
      //dpm('Removed expired/flushed '.$bin.' '.$cid); 
      return FALSE;
    }

    // Some systems don't update access time so we do it this way
    // XXX There's a chance that file no longer exists at this point
    // XXX but it's ok because we deal fine with broken cache entries
    // XXX should check only once in a page request if we have such
    // XXX filesystem and set $this->touch so that here we now what to do
    // XXX should be configurable
    //touch($filename);

    if ($cache->cid != $cid) {
      return FALSE;
    }
    return $cache;
  }

  function getMultiple(&$cids) {
    $results = array();
    foreach ($cids as $cid) {
      $cache = $this->get($cid);
      if ($cache !== FALSE) {
	$results[$cid] = $cache;
	unset($cids[$cid]);
      }
    }
    return $results;
  }

  function set($cid, $data, $expire = CACHE_PERMANENT) {
    $filename = $this->prefix . md5($cid);

    // Handle concurrent cache_set
    $i = 0;
    do {
      $tmp_filename = $filename .'.'. $i .'.tmp';
      $fh = fopen($tmp_filename, 'x');
      $i++;
    } while ($fh === FALSE);

    $cache = new StdClass;
    $cache->cid = $cid;
    $cache->created = REQUEST_TIME;
    $cache->expire = $expire;
    $cache->data = $data;
    fputs($fh, serialize($cache));
    fclose($fh);

    // It's OK if cache value is not available at some moment
    @unlink($filename); // Use @ because $filename may not exist
    rename($tmp_filename, $filename);
  }

  function clear($cid = NULL, $wildcard = FALSE) {
    if (is_string($cid) && !$wildcard) {
      $cid = array($cid);
    }
    if (is_array($cid)) {
      foreach ($cid as $one_cid) {
	$filename = $this->prefix . md5($one_cid);
	@unlink($filename); // Use @ because $one_cid may not exist
	//dpm('Remove '.$this->bin.' '.$one_cid);
      }
    } else {
      // XXX This result of this is as $conf['cache_lifetime'] = 0
      variable_set('filecache_flush_'. $this->bin, REQUEST_TIME);
      //dpm('Set filecache_flush_'. $this->bin);
    }
  }

  function isEmpty() {
    // XXX Not really used in Drupal core, only in tests
    return FALSE;
  }

}