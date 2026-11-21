#pragma once 

void setStimPinsToIdle(); 
void serviceStimTrain(); 
void stimService();
bool stimBusy(); 
void abortStim(); 
void startCommand(const StimCmd &cmd); 
