################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/libhelix/mp3dec.c \
../src/libhelix/mp3tabs.c 

C_DEPS += \
./src/libhelix/mp3dec.d \
./src/libhelix/mp3tabs.d 

OBJS += \
./src/libhelix/mp3dec.o \
./src/libhelix/mp3tabs.o 


# Each subdirectory must supply rules for building sources it contributes
src/libhelix/%.o: ../src/libhelix/%.c src/libhelix/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -DDEBUG -D__CODE_RED -DCORE_M3 -D__LPC17XX__ -D__NEWLIB__ -I"C:\Users\student\Documents\SQL Class\Odtwarzacz-MP3-main\Helix\inc" -O0 -fno-common -g3 -gdwarf-4 -Wall -c -fmessage-length=0 -fno-builtin -ffunction-sections -fdata-sections -fmerge-constants -fmacro-prefix-map="$(<D)/"= -mcpu=cortex-m3 -mthumb -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src-2f-libhelix

clean-src-2f-libhelix:
	-$(RM) ./src/libhelix/mp3dec.d ./src/libhelix/mp3dec.o ./src/libhelix/mp3tabs.d ./src/libhelix/mp3tabs.o

.PHONY: clean-src-2f-libhelix

