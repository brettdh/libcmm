# Intentional Networking

Intentional Networking (née libcmm*) is a library that helps
applications make good use of multiple networks simultaneously.

It presents an interface similar to the traditional
BSD socket API, with some additions to support multi-network
operation. Underneath this interface, Intentional Networking
maintains multiple connections as networks come and go,
sending traffic on the network that best suits the traffic's
characteristics. These characteristics are specified by
applications as simple, qualitative *labels* such as
"foreground" vs. "background."

In order to ensure that data is delivered in a correct order,
Intentional Networking allows applications to group data into
atomic units of delivery, called *IROBs* (Isolated Reliable
Ordered Bytestream). Applications can also specify ordering
constraints between IROBs, and Intentional Networking will
ensure that the receiving end sees the IROBs in an order that
obeys the constraints. This construction gives Intentional
Networking freedom to reorder and stripe data for performance
without breaking application expectations on delivery order.

For full details, see the [paper][intnw_paper] and
[presentation][intnw_talk] published in MobiCom 2010.

### How to use

Prerequisites:
- [Premake4](http://industriousone.com/premake/download)
- [Mocktime](http://github.com/brettdh/mocktime)
- [LibPowertutor](http://github.com/brettdh/libpowertutor)
- [Configumerator](http://github.com/brettdh/configumerator)
- [Instruments](http://github.com/brettdh/instruments)

For tests:
- [CppUnit](http://cppunit.sourceforge.net/doc/cvs/)

Build:

    $ git clone https://github.com/brettdh/libcmm
    $ cd libcmm
    $ make
    $ sudo make install

Use:

Here I must refer you to the API documentation in
`libcmm.h` and `libcmm_irob.h` and example
applications, such as `libcmm_test_{sender,receiver}.cpp`
and `AndroidSimpleSender`. I intend to write a more thorough
guide for setup and use eventually. I also intend to make
this more turnkey, but that will take considerable effort.

Did I mention this is research-quality code? The terms of the
[CRAPL][crapl] apply (though the actual license is BSD).

### Caveats

In order to actually use multiple networks at once on Linux,
your kernel must support *policy routing,* and you must configure
additional default gateways for each network interface, along
with a policy rule that directs traffic bound to a particular IP
to that gateway. (See above about how I really want to write
a better guide for all this, and then obviate the need for that
guide by making it all automatic.)

Good luck!

#### Footnote

\* "Connection Manager Manager" [sic]. It's a long story.


[intnw_paper]: http://bretthiggins.me/papers/mobicom10.pdf
[intnw_talk]: http://bretthiggins.me/papers/mobicom2010_slides_higgins.pptx
[crapl]: http://matt.might.net/articles/crapl/CRAPL-LICENSE.txt
