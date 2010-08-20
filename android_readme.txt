Ooookay.

connect() appeared to be hanging on the cellular interface when I have both interfaces turned on.  Maybe an effect of Android prefering WiFi? Need to investigate further.

I believe this has to do with the need for multiple default routes on a multi-homed host.  Next step is to test that out.

Maybe a deeper design problem is that I should be able to asynchronously add connections without waiting for slow connect()s to complete



wifi module wants kernel version string to be:
   '2.6.27-00393-g6607056 preempt mod_unload ARMv6 '


Whatever android does to enable each network interface is what I need to do.  No idea where to look, though.

SWEET!  Found the spot: in ConnectivityService (private Android framework class; gets compiled into /system/framework/services.jar).  Source lives in mydroid/frameworks/base/services/java/com/android/server.  Simple change to prevent the system from disabling 3G when wifi comes up.

Also, some testing with my cmm_test_sender toy app indicates that the routing setup is working.  Now just need to augment the connection scout to always be aware of the networks.