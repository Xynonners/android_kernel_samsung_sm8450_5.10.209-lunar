#!/bin/bash
# TEACAET-KERNEL-BUILD menu

# Variables
menu_version="v2.3"
DIR=`readlink -f .`
OUT_DIR=$DIR/out
PARENT_DIR=`readlink -f ${DIR}/..`

export CROSS_COMPILE=$PARENT_DIR/clang-r416183b/bin/aarch64-linux-gnu-
export CC=$PARENT_DIR/clang-r416183b/bin/clang

export PLATFORM_VERSION=12
export ANDROID_MAJOR_VERSION=s
export PATH=$PARENT_DIR/clang-r416183b/bin:$PATH
export PATH=$PARENT_DIR/build-tools/path/linux-x86:$PATH
export PATH=$PARENT_DIR/gas/linux-x86:$PATH
export TARGET_SOC=waipio
export LLVM=1 LLVM_IAS=1
export ARCH=arm64
#enable literally everything. Will this crash? I don't know :)
#https://github.com/LineageOS/android_prebuilts_clang_kernel_linux-x86_clang-r416183b/blob/54220fd601050b350b2af7adc913089ebf0e7aed/include/llvm/Support/AArch64TargetParser.def
#mtune is ignored on clang<14, need to fix later
#https://releases.llvm.org/14.0.0/tools/clang/docs/ReleaseNotes.html
export config KCFLAGS='
-Wfatal-errors
-pipe
-march=armv8.7-a+crc+lse+rdm+crypto+sm4+sha3+sha2+aes+dotprod+fp+simd+fp16+fp16fml+profile+ras+sve+sve2+sve2-aes+sve2-sm4+sve2-sha3+sve2-bitperm+rcpc+rng+memtag+ssbs+sb+predres+bf16+i8mm+f32mm+f64mm+tme+ls64+brbe+pauth+flagm
-mtune=cortex-a78
-mno-outline
-mno-outline-atomics
-fno-unroll-loops
'
KERNEL_MAKE_ENV="LOCALVERSION=-teacaet"

# Color
ON_BLUE=`echo -e "\033[44m"`	# On Blue
BRED=`echo -e "\033[1;31m"`	# Bold Red
BBLUE=`echo -e "\033[1;34m"`	# Bold Blue
BGREEN=`echo -e "\033[1;32m"`	# Bold Green
UNDER_LINE=`echo -e "\e[4m"`	# Text Under Line
STD=`echo -e "\033[0m"`		# Text Clear
 
# Functions
pause(){
  read -p "${BRED}$2${STD}Press ${BBLUE}[Enter]${STD} key to $1..." fackEnterKey
}

clang(){
  if [ ! -d $PARENT_DIR/clang-r416183b ]; then
    pause 'clone Android Clang/LLVM Prebuilts'
    git clone https://github.com/crdroidandroid/android_prebuilts_clang_host_linux-x86_clang-r416183b $PARENT_DIR/clang-r416183b
  fi
}

gas(){
  if [ ! -d $PARENT_DIR/gas/linux-x86 ]; then
    pause 'clone prebuilt binaries of GNU `as` (the assembler)'
    git clone https://android.googlesource.com/platform/prebuilts/gas/linux-x86 $PARENT_DIR/gas/linux-x86
  fi
}

build_tools(){
  if [ ! -d $PARENT_DIR/build-tools ]; then
    pause 'clone prebuilt binaries of build tools'
    git clone https://android.googlesource.com/platform/prebuilts/build-tools $PARENT_DIR/build-tools
  fi
}

variant(){
  findconfig=""
  findconfig=($(ls arch/arm64/configs/teacaet_* 2>/dev/null))
  declare -i i=1
  shift 2
  echo ""
  echo "${ON_BLUE}Variant Selection:${STD}"
  for e in "${findconfig[@]}"; do
    echo " $i. $(basename $e | cut -d'_' -f2)"
    i=i+1
  done
  local choice
  read -p "Enter choice [ 1 - $((i-1)) ] " choice
  i="$choice"
  if [[ $i -gt 0 && $i -le ${#findconfig[@]} ]]; then
    export v="${findconfig[$i-1]}"
    export VARIANT=$(basename $v | cut -d'_' -f2)
    echo ${VARIANT} selected
    pause 'continue'
  else
    pause 'return to Main menu' 'Invalid option, '
    return 1
  fi
}

clean(){
  echo "${BGREEN}***** Cleaning in Progress *****${STD}"
  make clean
  make mrproper
  [ -d "$OUT_DIR" ] && rm -rf $OUT_DIR
  echo "${BGREEN}***** Cleaning Done *****${STD}"
  pause 'continue'
}

load_config() {
  if [ ! -f "$OUT_DIR/.config" ]; then
    echo "No .config found, using default defconfig..."
    make -j$(nproc) -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV teacaet_${VARIANT}_defconfig
  else
    echo "Using existing .config..."
  fi
}

menuconfig(){
  variant || return 1
  echo "${BGREEN}***** Opening menuconfig *****${STD}"

  load_config

  make -j$(nproc) -C $(pwd) O=$(pwd)/out olddefconfig
  make -j$(nproc) -C $(pwd) O=$(pwd)/out menuconfig

  echo "${BGREEN}***** Configuration Saved *****${STD}"
  pause 'continue'
}

diff_config() {
  diff out/.config arch/arm64/configs/teacaet_mc-waipio-gki_defconfig
  pause 'continue'
}

save_config() {
  echo "${BGREEN}***** Saving config to arch/arm64/configs/teacaet_mc-waipio-gki_defconfig *****${STD}"
  if [ -f "$OUT_DIR/.config" ]; then
    cp -f $OUT_DIR/.config arch/arm64/configs/teacaet_mc-waipio-gki_defconfig
    pause 'continue'
  else
    pause 'return to Main menu' '.config does not exist'
    return 1
  fi
}

build_kernel(){
  variant || return 1
  echo "${BGREEN}***** Compiling kernel *****${STD}"
  [ ! -d "$OUT_DIR" ] && mkdir $OUT_DIR

  load_config

  make -j$(nproc) -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV

  [ -e $OUT_DIR/arch/arm64/boot/Image.gz ] && cp $OUT_DIR/arch/arm64/boot/Image.gz $OUT_DIR/Image.gz
  if [ -e $OUT_DIR/arch/arm64/boot/Image ]; then
    cp $OUT_DIR/arch/arm64/boot/Image $OUT_DIR/Image

    echo "${BGREEN}***** Ready to Roar *****${STD}"
    pause 'continue'
  else
    pause 'return to Main menu' 'Kernel STUCK in BUILD!, '
    return 1
  fi
}

anykernel3(){
  if [ ! -d $PARENT_DIR/AnyKernel3 ]; then
    pause 'clone AnyKernel3 - Flashable Zip Template'
    git clone https://github.com/osm0sis/AnyKernel3 $PARENT_DIR/AnyKernel3
  fi
  variant || return 1
  if [ -e $OUT_DIR/arch/arm64/boot/Image ]; then
    cd $PARENT_DIR/AnyKernel3
    git reset --hard
    cp $OUT_DIR/arch/arm64/boot/Image zImage
    sed -i "s/ExampleKernel by osm0sis/${VARIANT} kernel by teacaet/g" anykernel.sh
    sed -i "s/do\.devicecheck=1/do\.devicecheck=0/g" anykernel.sh
    sed -i "s/=maguro/=/g" anykernel.sh
    sed -i "s/=toroplus/=/g" anykernel.sh
    sed -i "s/=toro/=/g" anykernel.sh
    sed -i "s/=tuna/=/g" anykernel.sh
    sed -i "s/platform\/omap\/omap_hsmmc\.0\/by-name\/boot/bootdevice\/by-name\/boot/g" anykernel.sh
    sed -i "s/backup_file/#backup_file/g" anykernel.sh
    sed -i "s/replace_string/#replace_string/g" anykernel.sh
    sed -i "s/insert_line/#insert_line/g" anykernel.sh
    sed -i "s/append_file/#append_file/g" anykernel.sh
    sed -i "s/patch_fstab/#patch_fstab/g" anykernel.sh
    sed -i "s/dump_boot/split_boot/g" anykernel.sh
    sed -i "s/write_boot/flash_boot/g" anykernel.sh
    zip -r9 out/${VARIANT}_kernel_`cat $OUT_DIR/include/config/kernel.release`_`date '+%Y_%m_%d'`.zip * -x .git README.md *placeholder
    cd $DIR
    pause 'continue'
  else
    pause 'return to Main menu' 'Build kernel first, '
    return 1
  fi
}

# Run once
clang
gas
build_tools

# Show menu
show_menus(){
  clear
  echo "${ON_BLUE}TEACAET-KERNEL-BUILD menu $menu_version${STD}"
  echo " 1. ${UNDER_LINE}B${STD}uild kernel"
  echo " 2. ${UNDER_LINE}M${STD}enuconfig"
  echo " 3. ${UNDER_LINE}D${STD}iff config"
  echo " 4. ${UNDER_LINE}S${STD}ave config"
  echo " 5. ${UNDER_LINE}C${STD}lean"
  echo " 6. Make ${UNDER_LINE}f${STD}lashable zip"
  echo " 7. E${UNDER_LINE}x${STD}it"
}


# Read input
read_options(){
  local choice
  read -p "Enter choice [ 1 - 7 ] " choice
  case $choice in
    1|b|B) build_kernel ;;
    2|m|M) menuconfig ;;
    3|s|S) diff_config ;;
    4|s|S) save_config ;;
    5|c|C) clean ;;
    6|f|F) anykernel3 ;;
    7|x|X) exit 0 ;;
    *) pause 'return to Main menu' 'Invalid option, '
  esac
}

# Trap CTRL+C, CTRL+Z and quit singles
trap '' SIGINT SIGQUIT SIGTSTP
 
# Step # Main logic - infinite loop
while true
do
  show_menus
  read_options
done
