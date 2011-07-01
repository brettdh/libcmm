AC_INIT(libcmm)

dnl Checks for programs.
AC_PROG_CXX

dnl  Substitute the top of the object directory
TOPOBJ=`pwd`
AC_SUBST(TOPOBJ)

dnl  Enable packet_painting, if you want
AC_ARG_WITH([android-toolchain-root],
	    [AS_HELP_STRING([--with-android-toolchain-root],
	                    [Compile for Android with the specified toolchain])])
if test "x$with_android_toolchain_root" != "x" ; then
   echo "Configuring build for Android"
   CXX_PATH="$with_android_toolchain_root"
   CXX="arm-linux-androideabi-g++"
   CXXFLAGS="-DANDROID -DNDK_BUILD -mthumb"
   LDFLAGS="-Wl,--fix-cortex-a8"
   ANDROID="1"
else
   CXXFLAGS="-m32"
   ANDROID="0"
fi
AC_SUBST(CXX_PATH)
AC_SUBST(CXX)
AC_SUBST(CXXFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(ANDROID)

AC_OUTPUT(Makefile)
