# based on experimental code
IF (${PACKAGE_NAME}_ENABLE_Experimental)
  TRIBITS_ADD_EXAMPLE_DIRECTORIES(example)
  ADD_SUBDIRECTORY(src)
  TRIBITS_ADD_EXAMPLE_DIRECTORIES(test)
ENDIF()



