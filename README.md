Run android 4.22 on linux

1. Environment

  ubuntu 14.04

  jdk1.0.0.45 locate on /opt/jdk1.0.0_45 


2. How to build

  . env.sh
  
  source build/envsetup.sh
  
  lunch mini_x86-userdebug
  
  make -j4
  
  
3. How to run

  ./out/target/project/x86/system/runtime
  
  
