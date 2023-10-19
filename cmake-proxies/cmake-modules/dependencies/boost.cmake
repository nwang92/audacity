if(NOT TARGET boost::boost)
    set_target_properties(BOOST::BOOST PROPERTIES IMPORTED_GLOBAL TRUE)
    add_library(boost::boost ALIAS BOOST::BOOST)
endif()
