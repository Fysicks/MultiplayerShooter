// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectileRocket.h"
#include "Kismet/GameplayStatics.h"

AProjectileRocket::AProjectileRocket() {
	RocketMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rocket Mesh"));
	RocketMesh->SetupAttachment(RootComponent);
	RocketMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AProjectileRocket::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit) {
	// Apply damage
	APawn* FiringPawn = GetInstigator();
	if (FiringPawn) {
		AController* FiringController = FiringPawn->GetController();
		if (FiringController) {
			UGameplayStatics::ApplyRadialDamageWithFalloff(
				this,	// World context object
				Damage,	// Base damage
				10.f,	// Minimum damage
				GetActorLocation(),	//Origina of damaga radii
				DamageFalloffInnerRadius,	// Damage inner radius
				DamageFalloffOuterRadius,	// Damage outer radius
				1.f,	// Damage falloff (1.f means linear)
				UDamageType::StaticClass(), // Damage type class
				TArray<AActor*>(),	// Ignore actors (Could ignore shooter here)
				this,	// Damage causer
				FiringController // Instigator controller
			);
		}
	}


	// Does impact sounds and particles
	Super::OnHit(HitComp, OtherActor, OtherComp, NormalImpulse, Hit);


}