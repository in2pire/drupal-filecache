<?php

/**
 * Return data from the persistent cache.
 *
 * @param $key
 *   The cache ID of the data to retrieve.
 * @param $table
 *   The table $table to store the data in. Valid core values are 'cache_filter',
 *   'cache_menu', 'cache_page', or 'cache' for the default cache.
 */
function cache_get($key, $table = 'cache') {
  global $user;

  $src = variable_get('filecache_path', 'files/cache'). '/'. $table .'/'. urlencode($cid);
  if (!file_exists($src)) {
    return 0;
  }
  $cache = unserialize(file_get_contents($src));

  if (isset($cache->data)) {
    // If the data is permanent or we're not enforcing a minimum cache lifetime
    // always return the cached data.
    if ($cache->expire == CACHE_PERMANENT || !variable_get('cache_lifetime', 0)) {
      $cache->data = db_decode_blob($cache->data);
    }
    // If enforcing a minimum cache lifetime, validate that the data is
    // currently valid for this user before we return it by making sure the
    // cache entry was created before the timestamp in the current session's
    // cache timer. The cache variable is loaded into the $user object by
    // sess_read() in session.inc.
    else {
      if ($user->cache > $cache->created) {
        // This cache data is too old and thus not valid for us, ignore it.
        return 0;
      }
      else {
        $cache->data = db_decode_blob($cache->data);
      }
    }
    return $cache;
  }
  return 0;
}

/**
 * Store data in the persistent cache.
 *
 * The persistent cache is split up into four database
 * tables. Contributed modules can add additional tables.
 *
 * 'cache_page': This table stores generated pages for anonymous
 * users. This is the only table affected by the page cache setting on
 * the administrator panel.
 *
 * 'cache_menu': Stores the cachable part of the users' menus.
 *
 * 'cache_filter': Stores filtered pieces of content. This table is
 * periodically cleared of stale entries by cron.
 *
 * 'cache': Generic cache storage table.
 *
 * The reasons for having several tables are as follows:
 *
 * - smaller tables allow for faster selects and inserts
 * - we try to put fast changing cache items and rather static
 *   ones into different tables. The effect is that only the fast
 *   changing tables will need a lot of writes to disk. The more
 *   static tables will also be better cachable with MySQL's query cache
 *
 * @param $cid
 *   The cache ID of the data to store.
 * @param $table
 *   The table $table to store the data in. Valid core values are 'cache_filter',
 *   'cache_menu', 'cache_page', or 'cache'.
 * @param $data
 *   The data to store in the cache. Complex data types must be serialized first.
 * @param $expire
 *   One of the following values:
 *   - CACHE_PERMANENT: Indicates that the item should never be removed unless
 *     explicitly told to using cache_clear_all() with a cache ID.
 *   - CACHE_TEMPORARY: Indicates that the item should be removed at the next
 *     general cache wipe.
 *   - A Unix timestamp: Indicates that the item should be kept at least until
 *     the given time, after which it behaves like CACHE_TEMPORARY.
 * @param $headers
 *   A string containing HTTP header information for cached pages.
 */
function cache_set($cid, $table = 'cache', $data, $expire = CACHE_PERMANENT, $headers = NULL) {
  $cache = array(
    'data' => $data,
    'created' => time(),
    'expire' => $expire,
    'headers' => $headers,
  );
  $cache = serialize($cache);
  $path = variable_get('filecache_path', 'files/cache'). '/'.$table;
  if (!file_exists($path)) {
    mkdir($path, 0775, TRUE);
  }
  if (file_exists($path) && is_dir($path)) {
    $dest = $path .'/'. urlencode($cid);
    file_put_contents($dest, $cache);
  }
}

/**
 *
 * Expire data from the cache. If called without arguments, expirable
 * entries will be cleared from the cache_page table.
 *
 * @param $cid
 *   If set, the cache ID to delete. Otherwise, all cache entries that can
 *   expire are deleted.
 *
 * @param $table
 *   If set, the table $table to delete from. Mandatory
 *   argument if $cid is set.
 *
 * @param $wildcard
 *   If set to TRUE, the $cid is treated as a substring
 *   to match rather than a complete ID. The match is a right hand
 *   match. If '*' is given as $cid, the table $table will be emptied.
 */
function cache_clear_all($cid = NULL, $table = NULL, $wildcard = FALSE) {
  global $user;

  $path = variable_get('filecache_path', 'files/cache'). '/'. $table;

  if (!isset($cid) || !isset($table)) {
    cache_clear_all('*', !isset($table) ? 'cache_page' : $table);
    return;
  }

  if ($wildcard) {
    if (file_exists($path) && is_dir($path) && ($dir = opendir($path))) {
      if ($cid == '*') {
        $reg = '.*';
      }
      else {
        $reg = '^'. urlencode($cid);
      }
      while ($file = readdir($dir)) {
        if (!in_array($file, array('.', '..')) && ereg($reg, $file) !== FALSE && !is_dir($path. '/'. $file)) {
          unlink($path. '/'. $file);
        }
      }
    }
  }
  elseif (file_exists($path .'/'. urlencode($cid))) {
    unlink($path .'/'. urlencode($cid));
  }
}

