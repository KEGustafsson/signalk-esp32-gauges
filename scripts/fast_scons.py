"""Make SCons' up-to-date check cheap.

A 4" build with nothing to compile took 139 s, 80 s of them in "Building in
release mode": SCons hashed every one of ~1000 sources and the headers they
include, and looked each #include up again through ESP-IDF's hundreds of
include folders, on every run. Two standard SCons settings:

- Decider "MD5-timestamp": a file whose timestamp has not changed since the
  last build is taken as unchanged without hashing it; one that changed is
  still hashed, so touching a file without changing it rebuilds nothing.
- implicit_cache: the headers each source includes, found by the last scan,
  are reused instead of scanning again. A source's own change rescans it.
  The one case it misses: a new header placed earlier on the include path
  than the one a source found last time; `pio run -t clean` (or deleting
  .pio/build/<env>/.sconsign*.dblite) forgets the cache.
"""

Import("env")  # noqa: F821 -- injected by SCons

env.Decider("MD5-timestamp")  # noqa: F821
env.SetOption("implicit_cache", 1)  # noqa: F821
