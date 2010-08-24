Ooookay.

connect() appeared to be hanging on the cellular interface when I have both interfaces turned on.  Maybe an effect of Android prefering WiFi? Need to investigate further.

I believe this has to do with the need for multiple default routes on a multi-homed host.  Next step is to test that out.

Maybe a deeper design problem is that I should be able to asynchronously add connections without waiting for slow connect()s to complete



wifi module wants kernel version string to be:
   '2.6.27-00393-g6607056 preempt mod_unload ARMv6 '


Whatever android does to enable each network interface is what I need to do.  No idea where to look, though.

SWEET!  Found the spot: in ConnectivityService (private Android framework class; gets compiled into /system/framework/services.jar).  Source lives in mydroid/frameworks/base/services/java/com/android/server.  Simple change to prevent the system from disabling 3G when wifi comes up.

Also, some testing with my cmm_test_sender toy app indicates that the routing setup is working.  Now just need to augment the connection scout to always be aware of the networks.

8/24 TODO
Fixes needed in the connection scout:
1) Get list of networks at startup (DONE)
2) Do a down/up when the IP address of a network changes, rather than
   adding a new network with the new IP (DONE)
3) Make the scout do the routing table changes on startup and when
   the cellular IP address changes. (pending...)

8/25 TODO
I made a change to services.jar to avoid removing the default route for 
the TYPE_MOBILE network.  Now I need to get this gateway whenever I set up 
the TYPE_MOBILE network and swap it out for my custom gateway.  (It turns
out that the 'network' route can be the automatically set one; only the
custom gateway is needed to separate traffic.)

Also of interest: 
1) What happens when I'm moving around between cell towers?
   Do I get any down/up events?  Do I change IP addresses?
2) Are there any issues with the cellular network's NAT?
   It appears that the server (cmm_accept()-er) doesn't attempt
   to make connections, but I'm not sure that it never will,
   and if it does, they will fail for lack of route.
