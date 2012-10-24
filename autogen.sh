#! /bin/sh
unset love_4shared
case "$HOSTNAME" in
    dodge.relax.ru)
        love_4shared="--enable-4shared" ;;
esac

echo "*** Running autoheader"
autoheader || exit 1
echo "*** Running aclocal -I m4"
aclocal -I m4 || exit 1
echo "*** Running automake --add-missing --copy"
automake --add-missing --copy || exit 1
echo "*** Running autoconf"
autoconf || exit 1
./configure $love_4shared ${*}

