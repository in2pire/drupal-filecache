THIS SOFTWARE DOES NOT HAVE EVEN ALPHA VERSION. USE AT YOUR OWN RISK

Introduction
------------

This modules allows all Drupal caches to be stored in files instead of
DB. This is especially useful when shared hosting with slow RDBMS is
used. If you are on VPS, you need to consider apc or memcache modules
too. For simple cases and using memory filesystem, filecache is able
to outperform memcache.

Quick Installation
------------------

Install module as usual, e.g. by placing unpacked filecache directory
in sites/all/modules and enabling the module in
http://example.com/admin/modules. At the end of
sites/default/settings.php, add the following lines:

 /* Use filecache module */
 $conf['cache_backends'] = array('sites/all/modules/filecache/filecache.inc');
 $conf['cache_default_class'] = 'DrupalFileCache';
 #$conf['filecache_fast_pagecache'] = TRUE;

NOT IMPLEMENTED YET: filecache_fast_pagecache
---------------------------------------------

In the suggested lines for settings.php above, there's a commented
line for enabling pagecache support in filecache. To use it, just
remove the '#' character.

This mode of operation allows Drupal to serve cached pages without
accessing the database at all. The precise operation when
filecache_fast_pagecache is enabled is as follows:

1. Load cid 'variables' in bin 'cache' into $variables. If it doesn't
exist, no further action is taken.
2. Set the following $conf variables. Each variable is set only if
it's unset:
$conf['page_cache_without_database'] = TRUE;
$conf['page_cache_invoke_hooks'] = FALSE;
$conf['page_cache_maximum_age'] = $variables['page_cache_maximum_age'];
$conf['cache_lifetime'] = $variables['cache_lifetime'];
$conf['page_compression'] = $variables['page_compression'];

Cache directory
---------------

NOT IMPLEMENTED YET: By default, filecache uses 'filecache' directory
in site configuration directory (e.g. sites/default) for keeping cache
files. If Apache server is detected, an .htaccess file will be created
so that content of directory is not accessible through Apache. If
Apache is not detected and no directory is configured, filecache will
abort any page requests and demand proper configuration. SHOULD BE
INVESTIGATED: filecache module uses cron abilities of Drupal to
cleanup cache files that are expired by using cache lifetime
configuration setting.

You can set another directory with $conf['filecache_directory'] but in
this case the responsibility of cleaning up cache files is entirely up
to you. You may use memory filesystem for storage. Nowadays /tmp often
is such one. You may use ctime for decisions which files to be
removed. An example cron line is as follows (taken from Debian's
/etc/cron.d/php5):

19,49 * * * * [ -d /tmp/filecache ] && find /tmp/filecache/ -type f -cmin +30 -print0 | xargs -n 200 -r -0 rm

NOT IMPLEMENTED: 
Cache files
-----------

File names are in the format $bin-$md5_of_cid. (No file locking is used
and a cache directory can be shared via network between load-balanced
PHPs.) Modifying cache file is done by creating new file with
extension .tmp, unlinking the original one, and renaming the new file
to original name.

File content is serialized arrays with keys that mirror cache in DB:
cid, data, created, expire.

NOT IMPLEMENTED:
session_inc
