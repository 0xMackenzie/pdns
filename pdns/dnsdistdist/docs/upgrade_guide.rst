Upgrade Guide
=============

1.2.x to 1.3.x
--------------

In version 1.3.0, these things have changed.

The :ref:`Console` has an ACL now, which is set to ``{"127.0.0.0/8", "::1/128"}`` by default.
Add the appropriate :func:`setConsoleACL` and :func:`addConsoleACL` statements to the configuration file.

The ``--daemon`` option is removed from the :program:`dnsdist` binary, meaning that :program:`dnsdist` will not fork to the background anymore.
Hence, it can only be run on the foreground or under a supervisor like systemd, supervisord and ``daemon(8)``.

1.1.0 to 1.2.0
--------------

In 1.2.0, several configuration options have been changed:

As the amount of possible settings for listen sockets is growing, all listen-related options must now be passed as a table as the second argument to both :func:`addLocal` and :func:`setLocal`.
See the function's reference for more information.

The ``BlockFilter`` function is removed, as :func:`addRule` combined with a :func:`DropAction` can do the same.
