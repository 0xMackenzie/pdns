## PowerDNS Security Advisory 2015-01: Label decompression bug can cause crashes or CPU spikes

* CVE: CVE-2015-1868
* Date: 23rd of April 2015
* Credit: Aki Tuomi, Toshifumi Sakaguchi
* Affects: PowerDNS Recursor versions 3.5 and up; Authoritative Server 3.2 and up
* Not affected: Recursor 3.6.3; Recursor 3.7.2; Auth 3.3.2; Auth 3.4.4
* Severity: High
* Impact: Degraded service
* Exploit: This problem can be triggered by sending queries for specifically configured domains, or by sending specially crafted query packets
* Risk of system compromise: No
* Solution: Upgrade to any of the non-affected versions
* Workaround: Run your Recursor under a supervisor. Exposure can be limited by
  configuring the [`allow-from`](../recursor/settings.md#allow-from) setting so
  only trusted users can query your nameserver. There is no workaround for the
  Authoritative server.

A bug was discovered in our label decompression code, making it possible for
names to refer to themselves, thus causing a loop during decompression. On
some platforms, this bug can be abused to cause crashes. On all platforms,
this bug can be abused to cause service-affecting CPU spikes.

We recommend that all users upgrade to a corrected version if at all possible.
Alternatively, if you want to apply a minimal fix to your own tree, please
[find patches here](https://downloads.powerdns.com/patches/2015-01/).

As for workarounds, for the Recursor: only clients in allow-from are able to
trigger the degraded service, so this should be limited to your userbase;
further, we recommend running your critical services under supervision such
as systemd, supervisord, daemontools, etc.

There is no workaround for the Authoritative Server.

We want to thank Aki Tuomi for noticing this in production, and then digging
until he got to the absolute bottom of what at the time appeared to be a
random and spurious failure.

We want to thank Toshifumi Sakaguchi for further investigation into the issue
after the initial announcement, and for demonstrating to us quite clearly the
CPU spike issues.
