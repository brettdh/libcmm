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


Building native iproute2 tools...

  Don't forget to put these headers back!!
  1) bionic/libc/kernel/common/linux/rtnetlink.h
  2) frameworks/base/../utils.h
  Done.
  
  WTF: kernel headers inside bionic/libc/kernel/... don't match those inside
       kernel/include ???
      Missing #defines related to CAN (Controller Area Network)
      
  WTF answered: see bionic/libc/kernel/README.txt
     The Right Way to handle this is to go through the originals, patch them,
      and then run the script described in this README.
  
  Good news: the native, non-busybox version of ip route is able to display
     the entries in the custom routing table, giving me more confidence that
     they're actually there.  e.g. "ip route show table g1custom"
  Another mystery "solved": Android must internally be doing the equivalent of
     "ip route append" after the WiFi comes back up.  Doing this rather than
     "ip route add" allows me to add two default gateways at once.
     I think I still have to do the policy routing stuff, though.
     Haven't verified this.

Should be easy now to make the scout fix up the routing tables.
Pick up here next time.  Also need to figure out if the cellular NAT
is giving me problems.  Currently, I haven't been able to connect to
the external-facing IP address, but maybe that's some firewall issue at
the cell tower.

DONE! (9/29/2010)  The scout now does the routing table fixups.
Had to make the 'ip' command setuid.  If I were to steal
the Android platform code that modifies the routing tables (in order to make
this all less brittle), then the ConnScoutService would need root.
Next: make a test app that has two buttons: "SendFG" and "SendBG".  
Easy-to-understand multi-network test.

10/7/2010
Might need to take another look at the bit in services.jar that defaults
connections to WiFi when it's available; I think I may have changed the 
default for non-IntNW apps to cellular.
Also, enable "immediately destroy activities" in dev tools->devel settings
in order to test out why the ConnScout activity isn't getting the 
ConnScoutService instance when it starts up after being destroyed.

10/8/2010
Fixed the 10/7/2010 scout bug.  Sweet.
My simple-sender app is deadlocking, though, on the first BG packet.
Blocks forever in CMMThread::start, waiting for CMMThread::starter_mutex.
I wonder if that thread dies while holding starter_mutex?  That'd be the first
place to look, anyway.  It doesn't seem to be running at the point of 
deadlock, but I don't know yet when or why it died.  Turn on verbose
debug output for libcmm and see what's happening.
Addendum: the scout service seems to be dying occasionally.  it definitely
dies if it sees no output from an 'ip route/rule' command, but i'm not sure
yet when that happens.  Regardless, it should handle that gracefully.

10/9/2010
Check that; the exception in the scout seems to be when it parses the
cellular IP after the cellular network gets a disconnect intent (happens
right before it gets a connect intent; I'm not sure why that happens,
but I need to handle it).

10/11/2010
Triggered the scout bug again.  Somehow, the following happens:
  Log.d(TAG, ipStringToInt: address: " + ipAddr);
  => 26.48.64.122
  String blocks[] = ipAddr.split(".");
  => blocks.length == 1; blocks[0] = ""
Is that string getting modified in another thread somehow?
Possibility: I get a broadcast intent while I'm processing another one
and they concurrently modify the same NetUpdate object?  That doesn't appear
to be happening, though.  Need to track down where the object is modified;
I probably pass the string around a lot, and I need to be careful about 
whether it's by reference or by copy.


10/13/2010
Removed the call to ipStringToInt; since I don't care about the int 
representation anymore.  I convert the wifi int IP address to a string
and then deal in strings from that point forward.
If there was really a bug here, I didn't fix it; we'll see.
Extra weirdness: jdb showed a string like "a.b.c.d" but the result of
split(".") was {""}.  Meh.  Moving on to bigger fish for now.

10/19/2010
Made the change to services.jar to make sure the preferred network
(WiFi by default) is always "active" when it's connected.  More importantly,
make sure I still get up/down broadcasts for the "non-active" network.

10/20/2010
Android port is basically working.
Major pieces that remain:
  - Virgil-style tweaks to the network measurement stuff
    - Keep database of previously measured WiFi APs
    - Measure known WiFi networks less
    - Measure cellular network less frequently (more power-hungry)
    - Simple optimization: measure different networks simultaneously
      - Probably no effect on power consumption, but faster
      - Just as accurate if wireless hop is the bottleneck
        - Is that true for e.g. CAEN wireless?
    
  - Breadcrumbs-style network prediction
    - Store Markov model on phone
    - Train with some trace data (probably need to gather fresh, or maybe
        Timur has some I can use)
    - Update on-line with network measurements, GPS
    - Prediction querying API
    - Consideration: in the DARPA domain, mobility probably won't be 
        well-characterized by a Markov model.  However, it might be
        known ahead of time, and we might be able to ask people nearby
        about the wireless conditions.
    
  - Java wrapper for Intentional Networking API
    - Multisocket class
    - OutputStream replacement with API additions (labels, IROBs, thunks)
    - More wrappers as needed

  - Callable/Future API implementation
    - On its face, this seems like the most straightforward part
    - The "when-to-send-prefetch" decision is the tricky/interesting bit

  - PowerTutor power model
    - Find necessary hooks into Android system state
    - Build/borrow model and query API
