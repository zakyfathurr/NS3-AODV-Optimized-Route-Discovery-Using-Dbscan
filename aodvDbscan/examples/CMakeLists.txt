build_lib_example(
  NAME aodvDbscan-example
  SOURCE_FILES aodvDbscan.cc
  LIBRARIES_TO_LINK
    aodvDbscan
    ${libwifi}
    ${libinternet}
    ${libinternet_apps}
    ${libmobility}
    ${libapplications}
    ${libcore}
    ${libnetwork}
    ${libinternet-apps}
)
