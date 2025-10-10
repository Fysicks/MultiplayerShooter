// Fill out your copyright notice in the Description page of Project Settings.


#include "BlasterGameMode.h"
#include "Blaster/Character/BlasterCharacter.h"
#include "Blaster/PlayerController/BlasterPlayerController.h"

void ABlasterGameMode::PlayerEliminated(ABlasterCharacter* ElimmedCharacter, ABlasterPlayerController* VictimController, ABlasterPlayerController* AttackerController) {
	if (ElimmedCharacter) {
		UE_LOG(LogTemp, Warning, TEXT("PlayerEliminated is being called"))
		ElimmedCharacter->Elim();
	}
}
