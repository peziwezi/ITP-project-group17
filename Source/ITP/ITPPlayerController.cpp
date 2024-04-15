// Fill out your copyright notice in the Description page of Project Settings.


#include "ITPPlayerController.h"
void ITPPlayerController::BeginPlay()
{
    Super::BeginPlay();
    SetInputMode(FInputModeGameAndUI());
}
