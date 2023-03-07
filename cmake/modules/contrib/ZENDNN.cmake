# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

if(IS_DIRECTORY ${USE_ZEDNN})
	find_library(EXTERN_LIBRARY_ZENDNN NAMES zendnn HINTS ${USE_ZENDNN}/lib/)
	if (EXTERN_LIBRARY_ZENDNN STREQUAL "EXTERN_LIBRARY_ZENDNN-NOTFOUND")
		message(WARNING "Cannot find ZENDNN library at ${USE_ZENDNN}.")
  else()
		add_definitions(-DZEN_USE_JSON_RUNTIME=1)
		tvm_file_glob(GLOB ZENDNN_RELAY_CONTRIB_SRC src/relay/backend/contrib/zendnn/*.cc)
		list(APPEND COMPILER_SRCS ${ZENDNN_RELAY_CONTRIB_SRC})

		list(APPEND TVM_RUNTIME_LINKER_LIBS ${EXTERN_LIBRARY_ZENDNN})
		tvm_file_glob(GLOB ZENDNN_CONTRIB_SRC src/runtime/contrib/zendnn/zendnn_json_runtime.cc)
		list(APPEND RUNTIME_SRCS ${ZENDNN_CONTRIB_SRC})
		message(STATUS "Build with ZENDNN JSON runtime: " ${EXTERN_LIBRARY_ZENDNN})
  endif()
elseif((USE_ZENDNN STREQUAL "ON") OR (USE_ZENDNN STREQUAL "JSON"))
	add_definitions(-DZEN_USE_JSON_RUNTIME=1)
	tvm_file_glob(GLOB ZENDNN_RELAY_CONTRIB_SRC src/relay/backend/contrib/zendnn/*.cc)
	list(APPEND COMPILER_SRCS ${ZENDNN_RELAY_CONTRIB_SRC})

	#find_library(EXTERN_LIBRARY_ZENDNN zendnn)
	#list(APPEND TVM_RUNTIME_LINKER_LIBS ${EXTERN_LIBRARY_ZENDNN})
	tvm_file_glob(GLOB ZENDNN_CONTRIB_SRC src/runtime/contrib/zendnn/zendnn_json_runtime.cc)
	list(APPEND RUNTIME_SRCS ${ZENDNN_CONTRIB_SRC})
	message(STATUS "Build with ZENDNN JSON runtime: " ${EXTERN_LIBRARY_ZENDNN})
elseif(USE_ZENDNN STREQUAL "C_SRC")
	tvm_file_glob(GLOB ZENDNN_RELAY_CONTRIB_SRC src/relay/backend/contrib/zendnn/*.cc)
	list(APPEND COMPILER_SRCS ${ZENDNN_RELAY_CONTRIB_SRC})

	find_library(EXTERN_LIBRARY_ZENDNN zendnn)
	list(APPEND TVM_RUNTIME_LINKER_LIBS ${EXTERN_LIBRARY_ZENDNN})
	tvm_file_glob(GLOB ZENDNN_CONTRIB_SRC src/runtime/contrib/zendnn/zendnn.cc)
	list(APPEND RUNTIME_SRCS ${ZENDNN_CONTRIB_SRC})
	message(STATUS "Build with ZENDNN C source module: " ${EXTERN_LIBRARY_ZENDNN})
elseif(USE_ZENDNN STREQUAL "OFF")
  # pass
else()
	message(FATAL_ERROR "Invalid option: USE_ZENDNN=" ${USE_ZENNN})
endif()
