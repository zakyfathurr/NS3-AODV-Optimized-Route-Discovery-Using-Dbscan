set(source_files
    model/aodvDbscan-id-cache.cc
    model/aodvDbscan-dpd.cc
    model/aodvDbscan-rtable.cc
    model/aodvDbscan-rqueue.cc
    model/aodvDbscan-packet.cc
    model/aodvDbscan-neighbor.cc
    model/aodvDbscan-routing-protocol.cc
    helper/aodvDbscan-helper.cc
)

set(header_files
    model/aodvDbscan-id-cache.h
    model/aodvDbscan-dpd.h
    model/aodvDbscan-rtable.h
    model/aodvDbscan-rqueue.h
    model/aodvDbscan-packet.h
    model/aodvDbscan-neighbor.h
    model/aodvDbscan-routing-protocol.h
    helper/aodvDbscan-helper.h
)

build_lib(
  LIBNAME aodvDbscan
  SOURCE_FILES ${source_files}
  HEADER_FILES ${header_files}
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libinternet}
    ${libwifi}
    ${libapplications}
)

if (NS3_ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()