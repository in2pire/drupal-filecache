<?php

function filecache_directory() {
  $filecache_directory = variable_get('filecache_directory', FALSE);
  if ($filecache_directory === FALSE) {
    // Default directory for Apache only
    if (strpos($_SERVER['SERVER_SOFTWARE'], 'Apache') === 0) {
      $filecache_directory = DRUPAL_ROOT .'/'. conf_path() . '/filecache';
    }
  }
  return $filecache_directory;
}

class DrupalFileCache implements DrupalCacheInterface {

  function __construct($bin) {
    $this->bin = $bin;
    $filecache_directory = filecache_directory();
    if ($filecache_directory) {
      // Configured but is everything OK?
      if (is_dir($filecache_directory) || mkdir($filecache_directory)) {
	$htaccess = $filecache_directory . '/.htaccess';
	if (!file_exists($htaccess)) {
	  // Directory is not protected. Try to protect it
	  if (file_put_contents($htaccess, "Order allow,deny\n") === FALSE) {
	    // Failure in protecting it. Abort
	    $filecache_directory = FALSE;
	  }
	} elseif (!is_file($htaccess)) {
	  // Exists but it's not a regular file
	  $filecache_directory = FALSE;
	} // else $htaccess is a regular file and everything is OK
      } else {
	// Not a directory and mkdir failed
	$filecache_directory = FALSE;
      }
    }
    if ($filecache_directory === FALSE) {
      // This is serious. We've already took responsibility for this bin
      die('No filecache_directory and unability to setup default one.');
    }

    //@todo Handle when filecache_directory is not set
    //@todo Support custom prefix
    //@todo allow different prefix for cids
    $this->prefix = $filecache_directory .'/'. $bin .'-';
    $this->prefix_meta = $this->prefix;
  }

  function get($cid) {
    $md5 = md5($cid);
    $filename = $this->prefix . $md5;
    $filename_meta = $this->prefix_meta . $md5 . '.meta';

    // Use @ because cache entry may not exist
    $content = @file_get_contents($filename);
    if ($content === FALSE) {
      return FALSE;
    }
    $multi_cache = @unserialize($content);
    if ($multi_cache === FALSE) {
      // we are in the middle of cache_set
      $fh = fopen($filename, 'r');
      if ($fh === FALSE) {
	return FALSE;
      }
      if (flock($fh, LOCK_SH) === FALSE) {
	fclose($fh);
	return FALSE;
      }
      $multi_cache = @unserialize(@stream_get_contents($fh));
      if ($multi_cache === FALSE ||
	  flock($fh, LOCK_UN) === FALSE ||
	  fclose($fh) === FALSE) {
	unlink($filename); // remove broken file
	unlink($filename_meta);
	flock($fh, LOCK_UN);
	fclose($fh);
	return FALSE;
      }
    }

    if (is_object($multi_cache)) { // old alpha1 file format
      if ($multi_cache->cid != $cid) {
	return FALSE;
      }
      $cache =& $multi_cache;
    } else {
      if (!isset($multi_cache[$cid])) {
	return FALSE;
      }
      $cache =& $multi_cache[$cid];
    }

    // XXX Should reproduce the cache_lifetime / cache_flush_$bin logic
    $cache_flush = variable_get('filecache_flush_'. $this->bin, 0);
    if ($cache->expire != CACHE_TEMPORARY && // XXX how to handle this?
	$cache->expire != CACHE_PERMANENT &&
	($cache->expire < REQUEST_TIME ||
	 ($cache_flush && $cache->created < $cache_flush))) {
      // XXX For now this is the only place where cache entries are removed
      unlink($filename);
      unlink($filename_meta);
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
    $md5 = md5($cid);
    $filename = $this->prefix . $md5;
    $filename_meta = $this->prefix_meta . $md5 . '.meta';

    $fh = fopen($filename, 'c+b');
    if ($fh === FALSE) {
      // most likely permission error - report it as critical error
      watchdog('filecache', 'Canot open %filename',
	       array('%filename' => $filename), WATCHDOG_CRITICAL);
      return;
    }
    if (flock($fh, LOCK_EX) === FALSE) {
      fclose($fh);
      return;
    }
    $fh_meta = fopen($filename_meta, 'c+b');
    if ($fh_meta === FALSE) {
      watchdog('filecache', 'Canot open %filename',
	       array('%filename' => $filename_meta), WATCHDOG_CRITICAL);
      flock($fh, LOCK_UN);
      fclose($fh);
      return;
    }
    $meta = explode("\n", @stream_get_contents($fh_meta));
    if (is_array($meta) && (count($meta) != 1 || $meta[0] != $cid)) {
      $multi_cache = unserialize(stream_get_contents($fh));
      if ($multi_cache === FALSE) {
	$multi_cache = array();
      }
      elseif (is_object($multi_cache)) { // alpha1 file format
	$multi_cache = array($multi_cache->cid => $multi_cache);
      }
    } else {
      $multi_cache = array();
    }

    $cache = new StdClass;
    $cache->cid = $cid;
    $cache->created = REQUEST_TIME;
    $cache->expire = $expire;
    $cache->data = $data;
    $multi_cache[$cid] =& $cache;

    if (ftruncate($fh_meta, 0) === FALSE ||
	fwrite($fh_meta, implode("\n", array_keys($multi_cache))) === FALSE ||
	fclose($fh_meta) === FALSE) {
      // XXX should not happen -> cleanup
      unlink($filename_meta);
      unlink($filename);
      fclose($fh_meta);
      flock($fh, LOCK_UN);
      fclose($fh);
      return;
    }

    if (ftruncate($fh, 0) === FALSE ||
	fwrite($fh, serialize($multi_cache)) === FALSE ||
	flock($fh, LOCK_UN) === FALSE ||
	fclose($fh) === FALSE) {
      // XXX should not happen -> cleanup
      unlink($filename_meta);
      unlink($filename);
      //fclose($fh_meta); -- already closed
      flock($fh, LOCK_UN);
      fclose($fh);
      return;
    }
    
  }

  function clear($cid = NULL, $wildcard = FALSE) {
    if (is_string($cid) && !$wildcard) {
      $cid = array($cid);
    }
    if (is_array($cid)) {
      foreach ($cid as $one_cid) {
	$md5 = md5($one_cid);
	$filename = $this->prefix . $md5;
	$filename_meta = $this->prefix_meta . $md5 . '.meta';
	@unlink($filename); // Use @ because $one_cid may not exist
	@unlink($filename_meta); // Use @ because $one_cid may not exist
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