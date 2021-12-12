# export CMAKE_BUILD_TYPE=Debug
export PICO_SDK_PATH="./../../pico-sdk"
export BUILD="build_usb"
DT=`date +%Y%m%d%H%M`
mkdir -p ${BUILD}
# CFG_TUSB_DEBUG for debugging
# 0 : no debug
# 1 : print error
# 2 : print warning
# 3 : print info
# pico-sdk/lib/tinyusb/hw/bsp/rp2040/family.cmake
# export TINYUSB_DEBUG_LEVEL=3
# export CFG_TUSB_DEBUG=3 
cmake -S . -B ${BUILD} -DPICO_BUILD_DOCS=0  -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Debug
make V=1 DEBUG=1 -C ${BUILD} 2>&1 | tee -a build_${DT}.log