#!/usr/bin/env bash

aclocal \
&& automake --gnu --add-missing \
&& autoconf
