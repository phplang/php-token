dnl $Id$
dnl config.m4 for extension token

PHP_ARG_ENABLE(token, whether to enable token support,
[  --enable-token           Enable token support])

if test "$PHP_TOKEN" != "no"; then
  PHP_NEW_EXTENSION(token, token.c, $ext_shared)
fi
