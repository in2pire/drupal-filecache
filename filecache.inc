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
      $filecache_directory = filecache_directory();
      if ($filecache_directory === FALSE) {
?>
<p><strong>No filecache_directory and cannot setup default one because you are not running Apache.</strong></p>

<p>Please follow these steps:</p>
<ol>
<li>Make directory for <em>File Cache</em> that is writable by PHP (user <em><?php print get_current_user() ?></em>)</li>
<li>Add the following line to <em><?php print conf_path() . '/settings.php' ?></em>:
<pre>
$conf['filecache_directory'] = '<em>YOURDIRECTORYPATH</em>';
</pre></li>
</ol>
<?php
      } else {
	$filecache_directory = $conf_path . '/filecache';
?>
<p><strong>No filecache_directory and cannot setup default one for Apache.</strong></p>

<p>This is probably due to permission problem. There are two ways to resolve this.</p>

<ul>

<li>If you want to use default directory protected by Apache, run these commands as root:
<pre>
mkdir <?php print $filecache_directory ?>
chown <?php print get_current_user() ?>: <?php print $filecache_directory ?>
</pre>
</li>

<li>If you want to use another directory, make sure it's writable by PHP (user <em><?php print get_current_user() ?></em>) and add the following line to <em><?php print conf_path() . '/settings.php' ?></em>:
<pre>
$conf['filecache_directory'] = '<em>YOURDIRECTORYPATH</em>';
</pre>
</li>

</ul>

<?php
      }
      die();
    }

    //@todo Handle when filecache_directory is not set
    //@todo Support custom prefix
    //@todo allow different prefix for meta
    $this->prefix = $filecache_directory .'/'. $bin .'-';
    $this->prefix_meta = $this->prefix;
  }

  function get($cid) {
    $md5 = md5($cid);
    $filename = $this->prefix . $md5;
    $filename_meta = $this->prefix_meta . $md5 . '.meta'; // needed for unlink()

    // XXX should be in getMultiple() and get() to call getMultiple()
    if (! ($this->bin == 'cache' && $cid != 'variables')) { //XXX this is a hack XXX
      $this->delete_flushed();
    }

    // Use @ because cache entry may not exist
    $content = @file_get_contents($filename);
    if ($content === FALSE) {
      return FALSE;
    }
    $multi_cache = @unserialize($content);
    if ($multi_cache === FALSE) {
      // we are in the middle of cache_set
      $fh = fopen($filename, 'rb');
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
    if (! (is_array($meta) && count($meta) == 3 && $meta[1] == $cid)) {
      // we need to read cache file
      $multi_cache = unserialize(stream_get_contents($fh));
      if ($multi_cache === FALSE) { // XXX should not happen - what to do?
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

    // rebuild $meta
    $meta = array();
    $meta[] = count($multi_cache);
    foreach ($multi_cache as $cache) {
      $meta[] = $cache->cid;
      $meta[] = $cache->expire;
    }

    if (ftruncate($fh_meta, 0) === FALSE ||
	fwrite($fh_meta, implode("\n", $meta)) === FALSE ||
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
    global $user;

    // parts are shamelessy copied from includes/cache.inc

    if (empty($cid)) {
      if (variable_get('cache_lifetime', 0)) {
        // We store the time in the current user's $user->cache variable which
        // will be saved into the sessions bin by _drupal_session_write(). We then
        // simulate that the cache was flushed for this user by not returning
        // cached data that was cached before the timestamp.
        $user->cache = REQUEST_TIME;

        $cache_flush = variable_get('cache_flush_' . $this->bin, 0);
        if ($cache_flush == 0) {
          // This is the first request to clear the cache, start a timer.
          variable_set('cache_flush_' . $this->bin, REQUEST_TIME);
        }
        elseif (REQUEST_TIME > ($cache_flush + variable_get('cache_lifetime', 0))) {
          // Clear the cache for everyone, cache_lifetime seconds have
          // passed since the first request to clear the cache.
	  $this->delete_expired();
          variable_set('cache_flush_' . $this->bin, 0);
        }
      }
      else {
        // No minimum cache lifetime, flush all temporary cache entries now.
	$this->delete_expired();
      }
    }
    else {
      if ($wildcard) {
        if ($cid == '*') {
	  $this->delete_all();
        }
        else {
	  $this->delete_wildcard($cid);
        }
      }
      elseif (is_array($cid)) {
        // Delete in chunks when a large array is passed.
	foreach ($cid as $one_cid) {
	  $this->delete_one($one_cid);
	}
      }
      else {
	$this->delete_one($cid);
      }
    }
  }

  protected function delete_all() {
    foreach (glob($this->prefix . '*.meta', GLOB_NOSORT) as $filename) { //XXX on error?
      $filename_meta = $filename . '.meta'; //XXX assumes prefix == prefix_meta
      
      // XXX is order important here?
      @unlink($filename);
      @unlink($filename_meta);
    }
  }

  protected function delete_one($cid) {
    $md5 = md5($cid);
    $filename = $this->prefix . $md5;
    $filename_meta = $this->prefix_meta . $md5 . '.meta';

    // XXX is order important here?
    @unlink($filename);
    @unlink($filename_meta);
  }

  // helper function, $return[$filename][$cid] = $expire
  protected function glob_meta() {
    $result = array();
    foreach (glob($this->prefix_meta . '*.meta', GLOB_NOSORT) as $filename_meta) { //XXX on error?
      $meta_content = @file_get_contents($filename_meta);
      if ($meta_content === FALSE) {
	continue; // XXX should rebuild?
      }
      $meta = explode("\n", $meta_content);
      if (!is_array($meta) ||
	  count($meta) % 2 != 1 || // 1 line for count + 2 lines for each (cid,expire)
	  strval(intval($meta[0])) != $meta[0] || // first line must be int
	  intval($meta[0]) * 2 + 1 != count($meta)) { // see above
	continue; // XXX should rebuild?
      }
      $filename = preg_replace('/\.meta$/', '', $filename_meta);
      $result[$filename] = array();
      for ($i = 1; $i < count($meta); $i += 2) {
	// $result[$filename][$cid] = $expire
	$result[$filename][$meta[$i]] = intval($meta[$i+1]);
      }
    }
    return $result;
  }

  protected function delete_expired() {
    foreach ($this->glob_meta() as $filename => $cids) {
      $filename_meta = $filename . '.meta'; //XXX assumes prefix == prefix_meta
      foreach ($cids as $cid => $expire) {
	if ($expire == CACHE_PERMANENT) {
	  continue;
	}
	if ($expire == CACHE_TEMPORARY ||
	    $expire < REQUEST_TIME) {
	  //XXX is order important?
	  @unlink($filename);
	  @unlink($filename_meta);
	  break;
	}
      }
    }
  }

  protected function delete_flushed() {
    // Garbage collection necessary when enforcing a minimum cache lifetime.
    $cache_flush = variable_get('cache_flush_' . $this->bin, 0);
    if ($cache_flush && ($cache_flush + variable_get('cache_lifetime', 0) <= REQUEST_TIME)) {
      // Reset the variable immediately to prevent a meltdown in heavy load situations.
      variable_set('cache_flush_' . $this->bin, 0);
      // Time to flush old cache data
      foreach ($this->glob_meta() as $filename => $cids) {
	$filename_meta = $filename . '.meta'; //XXX assumes prefix == prefix_meta
	foreach ($cids as $cid => $expire) {
	  if ($expire != CACHE_PERMANENT &&
	      $expire <= $cache_flush) {
	    //XXX is order important?
	    @unlink($filename);
	    @unlink($filename_meta);
	    break;
	  }
	}
      }
    }
  }

  protected function delete_wildcard($cid_prefix) {
    foreach ($this->glob_meta() as $filename => $cids) {
      $filename_meta = $filename . '.meta'; //XXX assumes prefix == prefix_meta
      foreach ($cids as $cid => $expire) {
	if (strpos($cid, $cid_prefix) === 0) { //XXX could be more optimized
	  //XXX is order important?
	  @unlink($filename);
	  @unlink($filename_meta);
	  break;
	}
      }
    }
  }

  function isEmpty() {
    return count($this->glob_meta()) == 0;
  }

}

