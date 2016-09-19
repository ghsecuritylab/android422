#ANDROID_BUILD_TOP:=/home/k0059/build/android422
android_dir_dx = $(ANDROID_BUILD_TOP)/out/host/linux-x86/bin/dx

all:
	javac hello.java
	$(android_dir_dx) --dex --output=hello.jar hello.class

clean:
	@rm *.jar *.class
