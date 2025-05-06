################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/ff.c \
../src/mmc.c 

C_DEPS += \
./src/ff.d \
./src/mmc.d 

OBJS += \
./src/ff.o \
./src/mmc.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -DDEBUG -D__CODE_RED -D__REDLIB__ -I"C:\Users\student\Documents\SQL Class\Odtwarzacz-MP3-main\Lib_CMSISv1p30_LPC17xx\inc" -I"C:\Users\student\Documents\SQL Class\Odtwarzacz-MP3-main\Lib_FatFs_SD\inc" -I"C:\Users\student\Documents\SQL Class\Odtwarzacz-MP3-main\Lib_MCU\inc" -O0 -g3 -gdwarf-4 -Wall -c -fmessage-length=0 -fno-builtin -ffunction-sections -fmerge-constants -fmacro-prefix-map="$(<D)/"= -mcpu=cortex-m3 -mthumb -D__REDLIB__ -fstack-usage -specs=redlib.specs -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/ff.d ./src/ff.o ./src/mmc.d ./src/mmc.o

.PHONY: clean-src

