################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/bitstream.c \
../src/buffers.c \
../src/cr_startup_lpc17.c \
../src/dct32.c \
../src/dequant.c \
../src/dqchan.c \
../src/fattime.c \
../src/helix_mp3.c \
../src/huffman.c \
../src/hufftabs.c \
../src/imdct.c \
../src/main.c \
../src/mp3dec.c \
../src/mp3tabs.c \
../src/polyphase.c \
../src/scalfact.c \
../src/stproc.c \
../src/subband.c \
../src/testme.c \
../src/trigtabs.c 

C_DEPS += \
./src/bitstream.d \
./src/buffers.d \
./src/cr_startup_lpc17.d \
./src/dct32.d \
./src/dequant.d \
./src/dqchan.d \
./src/fattime.d \
./src/helix_mp3.d \
./src/huffman.d \
./src/hufftabs.d \
./src/imdct.d \
./src/main.d \
./src/mp3dec.d \
./src/mp3tabs.d \
./src/polyphase.d \
./src/scalfact.d \
./src/stproc.d \
./src/subband.d \
./src/testme.d \
./src/trigtabs.d 

OBJS += \
./src/bitstream.o \
./src/buffers.o \
./src/cr_startup_lpc17.o \
./src/dct32.o \
./src/dequant.o \
./src/dqchan.o \
./src/fattime.o \
./src/helix_mp3.o \
./src/huffman.o \
./src/hufftabs.o \
./src/imdct.o \
./src/main.o \
./src/mp3dec.o \
./src/mp3tabs.o \
./src/polyphase.o \
./src/scalfact.o \
./src/stproc.o \
./src/subband.o \
./src/testme.o \
./src/trigtabs.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -DDEBUG -D__USE_CMSIS=CMSISv1p30_LPC17xx -D__CODE_RED -D__NEWLIB__ -I"C:\Users\student\Documents\Odtwarzacz-MP3\Lib_CMSISv1p30_LPC17xx\inc" -I"C:\Users\student\Documents\Odtwarzacz-MP3\Lib_EaBaseBoard\inc" -I"C:\Users\student\Documents\Odtwarzacz-MP3\Lib_FatFs_SD\inc" -I"C:\Users\student\Documents\Odtwarzacz-MP3\Lib_MCU\inc" -O0 -g3 -gdwarf-4 -Wall -c -fmessage-length=0 -fno-builtin -ffunction-sections -fmerge-constants -fmacro-prefix-map="$(<D)/"= -mcpu=cortex-m3 -mthumb -D__NEWLIB__ -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/bitstream.d ./src/bitstream.o ./src/buffers.d ./src/buffers.o ./src/cr_startup_lpc17.d ./src/cr_startup_lpc17.o ./src/dct32.d ./src/dct32.o ./src/dequant.d ./src/dequant.o ./src/dqchan.d ./src/dqchan.o ./src/fattime.d ./src/fattime.o ./src/helix_mp3.d ./src/helix_mp3.o ./src/huffman.d ./src/huffman.o ./src/hufftabs.d ./src/hufftabs.o ./src/imdct.d ./src/imdct.o ./src/main.d ./src/main.o ./src/mp3dec.d ./src/mp3dec.o ./src/mp3tabs.d ./src/mp3tabs.o ./src/polyphase.d ./src/polyphase.o ./src/scalfact.d ./src/scalfact.o ./src/stproc.d ./src/stproc.o ./src/subband.d ./src/subband.o ./src/testme.d ./src/testme.o ./src/trigtabs.d ./src/trigtabs.o

.PHONY: clean-src

