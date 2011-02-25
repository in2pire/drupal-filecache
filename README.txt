
Introduction
------------
This is a replacement cache for Drupal which instead of placing the cachable
objects into the database which is done by default it writes the objects
into the filesystem instead.

In cases where the website is a dedicated server or a VPS where you have
control over the core system, then this is not for you. Check out the
memcache project http://drupal.org/project/memcache

If you are on a shared host where you have slow database servers then this
is for you.
