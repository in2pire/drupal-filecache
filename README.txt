Introduction
------------

This modules allows Drupal caches to be stored in files instead of
storing in database.

Comparison with other caching modules
-------------------------------------

memcache is the closest caching module. File Cache can be configured
to use memory filesystem (e.g. /dev/shm in Debian) which is very close
to what memcache do. File Cache can use network filesystems and this
is another use case where memcache is traditionally used.

apc can be used for cache bins too. If it's used for that purpose,
it's usually only for some of the critical cache bins like cache and
cache_bootstrap.

boost generates caches of pages that are directly served by web
server. File Cache can plug in regular Drupal page caching and provide
very fast page caching but this still needs a bit of PHP to be
executed. Database access can be avoided altogether though. See
$conf['filecache_fast_pagecache'] below.

Feedback
--------

Benchmark results in real scenarios are needed in comparison against
memcache, apc and boost. If you have such experience, please share
with File Cache maintainer 'ogi' (Ognyan Kulev). Numbers in
comparison with memcache against File Cache on memory filesystem are
especially important.

Quick Installation
------------------

Install module as usual, e.g. by placing unpacked filecache directory
in sites/all/modules and enabling the module in admin/modules. Go to
admin/reports/status and follow the instructions. These instructions
are meant to be complete so if they are not enough, file issue against
File Cache module.

NOT IMPLEMENTED YET:
filecache_fast_pagecache
------------------------

 #$conf['filecache_fast_pagecache'] = TRUE;

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

If $conf['filecache_directory'] is not configured and web server is
Apache,, File Cache uses 'filecache' directory in site configuration
directory (e.g. sites/default) for keeping cache files. An .htaccess
file will be created so that content of directory is not accessible
through Apache.

If Apache is not detected and no directory is configured, filecache
will abort any page requests and demand proper configuration.

Cache files
-----------

File names are in the format $bin-$md5_of_cid. (No file locking is
used but this will be changed.) Modifying cache file is done by
creating new file with extension .NUMBER.tmp, unlinking the original one, and
renaming the new file to original name.

File content is serialized stdClass with keys that mirror cache in DB:
cid, data, created, expire.

NOT IMPLEMENTED:
Replacement for includes/session_inc
------------------------------------

Using File Cache for storing sessions.

BUGS
----

When switching back to DB/PostgreSQL back end, some DB locking
occurs. Need to investigate.

cache_clear_all ignores cache_lifetime.

cache_clear_all doesn't properly handle $wildcard=TRUE. How to fix it?

File Cache directory can grow indefinetely.

TODO
----

Using flock will be faster (for cache_set) and more reliable.

Recommend using memory filesystem.
