if(NOT TARGET openssl::openssl)
    set_target_properties(OpenSSL::OpenSSL PROPERTIES IMPORTED_GLOBAL TRUE)
    add_library(openssl::openssl ALIAS OpenSSL::OpenSSL)
endif()
