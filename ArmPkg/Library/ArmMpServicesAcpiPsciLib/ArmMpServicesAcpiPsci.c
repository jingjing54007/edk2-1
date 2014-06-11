/** @file
*
*  Copyright (c) 2013-2014, ARM Limited. All rights reserved.
*
*  This program and the accompanying materials
*  are licensed and made available under the terms and conditions of the BSD License
*  which accompanies this distribution.  The full text of the license may be found at
*  http://opensource.org/licenses/bsd-license.php
*
*  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
*  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*
**/

#include "ArmMpServicesInternal.h"

#define PATCH_MAILBOX_DATA(Type, ProcessorNumber, Variable, Value) \
  *(Type*)((UINTN)mMpProcessorInfo[ProcessorNumber].Mailbox + (UINTN)&(Variable) - (UINTN)MailboxCodeStart) = (Type)(Value);

EFI_CPU_ARCH_PROTOCOL   *mCpu = NULL;

/*
 * Variable and Symbols exported by MpServicesHelper.S
 */
extern UINT64 MailboxCodeStack;
extern UINT64 MailboxCodeProcessorIdOffset;
extern UINT64 MailboxCodeJumpAddressOffset;

VOID
MailboxCodeStart (
  VOID
  );

VOID
MailboxCodeEnd (
  VOID
  );

ARM_PROCESSOR_TABLE *mArmProcessorTable;
PROCESSOR_INFO *mMpProcessorInfo;

/**
  This return the handle number for the calling processor.  This service may be
  called from the BSP and APs.

  This service returns the processor handle number for the calling processor.
  The returned value is in the range from 0 to the total number of logical
  processors minus 1. The total number of logical processors can be retrieved
  with EFI_MP_SERVICES_PROTOCOL.GetNumberOfProcessors(). This service may be
  called from the BSP and APs. If ProcessorNumber is NULL, then EFI_INVALID_PARAMETER
  is returned. Otherwise, the current processors handle number is returned in
  ProcessorNumber, and EFI_SUCCESS is returned.

  @param[in] This              A pointer to the EFI_MP_SERVICES_PROTOCOL instance.
  @param[in] ProcessorNumber   The handle number of AP that is to become the new
                               BSP. The range is from 0 to the total number of
                               logical processors minus 1. The total number of
                               logical processors can be retrieved by
                               EFI_MP_SERVICES_PROTOCOL.GetNumberOfProcessors().

  @retval EFI_SUCCESS             The current processor handle number was returned
                                  in ProcessorNumber.
  @retval EFI_INVALID_PARAMETER   ProcessorNumber is NULL.

**/
EFI_STATUS
MpWhoAmI (
  IN EFI_MP_SERVICES_PROTOCOL  *This,
  OUT UINTN                    *ProcessorNumber
  )
{
  UINTN Index;
  UINTN MpId;
  UINTN ClusterId;
  UINTN CoreId;

  if (ProcessorNumber == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  MpId      = ArmReadMpidr ();
  ClusterId = GET_CLUSTER_ID (MpId);
  CoreId    = GET_CORE_ID (MpId);

  for (Index = 0; Index < mArmProcessorTable->NumberOfEntries; Index++) {
    if ((mArmProcessorTable->ArmCpus[Index].ClusterId == ClusterId) &&
        (mArmProcessorTable->ArmCpus[Index].CoreId == CoreId))
    {
      *ProcessorNumber = Index;
      return EFI_SUCCESS;
    }
  }

  ASSERT (0);
  return EFI_INVALID_PARAMETER;
}

/**
  This service retrieves the number of logical processor in the platform
  and the number of those logical processors that are enabled on this boot.
  This service may only be called from the BSP.

  This function is used to retrieve the following information:
    - The number of logical processors that are present in the system.
    - The number of enabled logical processors in the system at the instant
      this call is made.

  Because MP Service Protocol provides services to enable and disable processors
  dynamically, the number of enabled logical processors may vary during the
  course of a boot session.

  If this service is called from an AP, then EFI_DEVICE_ERROR is returned.
  If NumberOfProcessors or NumberOfEnabledProcessors is NULL, then
  EFI_INVALID_PARAMETER is returned. Otherwise, the total number of processors
  is returned in NumberOfProcessors, the number of currently enabled processor
  is returned in NumberOfEnabledProcessors, and EFI_SUCCESS is returned.

  @param[in]  This                        A pointer to the EFI_MP_SERVICES_PROTOCOL
                                          instance.
  @param[out] NumberOfProcessors          Pointer to the total number of logical
                                          processors in the system, including the BSP
                                          and disabled APs.
  @param[out] NumberOfEnabledProcessors   Pointer to the number of enabled logical
                                          processors that exist in system, including
                                          the BSP.

  @retval EFI_SUCCESS             The number of logical processors and enabled
                                  logical processors was retrieved.
  @retval EFI_DEVICE_ERROR        The calling processor is an AP.
  @retval EFI_INVALID_PARAMETER   NumberOfProcessors is NULL.
  @retval EFI_INVALID_PARAMETER   NumberOfEnabledProcessors is NULL.

**/
EFI_STATUS
MpGetNumberOfProcessors (
  IN  EFI_MP_SERVICES_PROTOCOL  *This,
  OUT UINTN                     *NumberOfProcessors,
  OUT UINTN                     *NumberOfEnabledProcessors
  )
{
  UINTN        Index;
  ARM_SMC_ARGS SmcArgs;
  UINTN        Enabled;

  if (NumberOfProcessors != NULL) {
    *NumberOfProcessors = mArmProcessorTable->NumberOfEntries;
  }

  if (NumberOfEnabledProcessors != NULL) {
    Enabled = 0;

    for (Index = 0; Index < mArmProcessorTable->NumberOfEntries; Index++) {
#ifdef MDE_CPU_AARCH64
      SmcArgs.Arg0 = ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH64;
      SmcArgs.Arg1 = ARM_SMC_PSCI_TARGET_CPU64 (0, 0, mArmProcessorTable->ArmCpus[Index].ClusterId, mArmProcessorTable->ArmCpus[Index].CoreId);
#else
      SmcArgs.Arg0 = ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH32;
      SmcArgs.Arg1 = ARM_SMC_PSCI_TARGET_CPU32 (0, mArmProcessorTable->ArmCpus[Index].ClusterId, mArmProcessorTable->ArmCpus[Index].CoreId);
#endif
      SmcArgs.Arg2 = ARM_SMC_ID_PSCI_AFFINITY_LEVEL_0;
      ArmCallSmc (&SmcArgs);
      if (SmcArgs.Arg0 == ARM_SMC_ID_PSCI_AFFINITY_INFO_ON) {
        Enabled++;
      }
    }

    // The boot CPU must at least be on
    ASSERT (Enabled > 0);
    *NumberOfEnabledProcessors = Enabled;
  }

  return EFI_SUCCESS;
}

/**
  Gets detailed MP-related information on the requested processor at the
  instant this call is made. This service may only be called from the BSP.

  This service retrieves detailed MP-related information about any processor
  on the platform. Note the following:
    - The processor information may change during the course of a boot session.
    - The information presented here is entirely MP related.

  Information regarding the number of caches and their sizes, frequency of operation,
  slot numbers is all considered platform-related information and is not provided
  by this service.

  @param[in]  This                  A pointer to the EFI_MP_SERVICES_PROTOCOL
                                    instance.
  @param[in]  ProcessorNumber       The handle number of processor.
  @param[out] ProcessorInfoBuffer   A pointer to the buffer where information for
                                    the requested processor is deposited.

  @retval EFI_SUCCESS             Processor information was returned.
  @retval EFI_DEVICE_ERROR        The calling processor is an AP.
  @retval EFI_INVALID_PARAMETER   ProcessorInfoBuffer is NULL.
  @retval EFI_NOT_FOUND           The processor with the handle specified by
                                  ProcessorNumber does not exist in the platform.

**/
EFI_STATUS
MpGetProcessorInfo (
  IN  EFI_MP_SERVICES_PROTOCOL   *This,
  IN  UINTN                      ProcessorNumber,
  OUT EFI_PROCESSOR_INFORMATION  *ProcessorInfoBuffer
  )
{
  EFI_STATUS    Status;
  ARM_SMC_ARGS  SmcArgs;
  ARM_CORE_INFO *ArmCpu;
  UINTN         IAm;

  if (ProcessorInfoBuffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (ProcessorNumber > mArmProcessorTable->NumberOfEntries) {
    return EFI_INVALID_PARAMETER;
  }

  ArmCpu = &mArmProcessorTable->ArmCpus[ProcessorNumber];

  Status = MpWhoAmI (This, &IAm);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ProcessorInfoBuffer->StatusFlag = PROCESSOR_HEALTH_STATUS_BIT;
  if (IAm == ProcessorNumber) {
    ProcessorInfoBuffer->StatusFlag |= PROCESSOR_AS_BSP_BIT;
  }
#ifdef MDE_CPU_AARCH64
  SmcArgs.Arg0 = ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH64;
  SmcArgs.Arg1 = ARM_SMC_PSCI_TARGET_CPU64 (0, 0, ArmCpu->ClusterId, ArmCpu->CoreId);
#else
  SmcArgs.Arg0 = ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH32;
  SmcArgs.Arg1 = ARM_SMC_PSCI_TARGET_CPU32 (0, ArmCpu->ClusterId, ArmCpu->CoreId);
#endif
  SmcArgs.Arg2 = ARM_SMC_ID_PSCI_AFFINITY_LEVEL_0;
  ArmCallSmc (&SmcArgs);
  //Note: We consider the state ON_PENDING as ON
  if ((SmcArgs.Arg0 == ARM_SMC_ID_PSCI_AFFINITY_INFO_ON) || (SmcArgs.Arg0 == ARM_SMC_ID_PSCI_AFFINITY_INFO_ON_PENDING)) {
    ProcessorInfoBuffer->StatusFlag |= PROCESSOR_ENABLED_BIT;
  }

  ProcessorInfoBuffer->ProcessorId = GET_MPID (ArmCpu->ClusterId, ArmCpu->CoreId);
  ProcessorInfoBuffer->Location.Package = ArmCpu->ClusterId;
  ProcessorInfoBuffer->Location.Core    = ArmCpu->CoreId;
  ProcessorInfoBuffer->Location.Thread  = 0;

  return EFI_SUCCESS;
}


/**
  This service executes a caller provided function on all enabled APs. APs can
  run either simultaneously or one at a time in sequence. This service supports
  both blocking and non-blocking requests. The non-blocking requests use EFI
  events so the BSP can detect when the APs have finished. This service may only
  be called from the BSP.

  This function is used to dispatch all the enabled APs to the function specified
  by Procedure.  If any enabled AP is busy, then EFI_NOT_READY is returned
  immediately and Procedure is not started on any AP.

  If SingleThread is TRUE, all the enabled APs execute the function specified by
  Procedure one by one, in ascending order of processor handle number. Otherwise,
  all the enabled APs execute the function specified by Procedure simultaneously.

  If WaitEvent is NULL, execution is in blocking mode. The BSP waits until all
  APs finish or TimeoutInMicroSecs expires. Otherwise, execution is in non-blocking
  mode, and the BSP returns from this service without waiting for APs. If a
  non-blocking mode is requested after the UEFI Event EFI_EVENT_GROUP_READY_TO_BOOT
  is signaled, then EFI_UNSUPPORTED must be returned.

  If the timeout specified by TimeoutInMicroseconds expires before all APs return
  from Procedure, then Procedure on the failed APs is terminated. All enabled APs
  are always available for further calls to EFI_MP_SERVICES_PROTOCOL.StartupAllAPs()
  and EFI_MP_SERVICES_PROTOCOL.StartupThisAP(). If FailedCpuList is not NULL, its
  content points to the list of processor handle numbers in which Procedure was
  terminated.

  Note: It is the responsibility of the consumer of the EFI_MP_SERVICES_PROTOCOL.StartupAllAPs()
  to make sure that the nature of the code that is executed on the BSP and the
  dispatched APs is well controlled. The MP Services Protocol does not guarantee
  that the Procedure function is MP-safe. Hence, the tasks that can be run in
  parallel are limited to certain independent tasks and well-controlled exclusive
  code. EFI services and protocols may not be called by APs unless otherwise
  specified.

  In blocking execution mode, BSP waits until all APs finish or
  TimeoutInMicroSeconds expires.

  In non-blocking execution mode, BSP is freed to return to the caller and then
  proceed to the next task without having to wait for APs. The following
  sequence needs to occur in a non-blocking execution mode:

    -# The caller that intends to use this MP Services Protocol in non-blocking
       mode creates WaitEvent by calling the EFI CreateEvent() service.  The caller
       invokes EFI_MP_SERVICES_PROTOCOL.StartupAllAPs(). If the parameter WaitEvent
       is not NULL, then StartupAllAPs() executes in non-blocking mode. It requests
       the function specified by Procedure to be started on all the enabled APs,
       and releases the BSP to continue with other tasks.
    -# The caller can use the CheckEvent() and WaitForEvent() services to check
       the state of the WaitEvent created in step 1.
    -# When the APs complete their task or TimeoutInMicroSecondss expires, the MP
       Service signals WaitEvent by calling the EFI SignalEvent() function. If
       FailedCpuList is not NULL, its content is available when WaitEvent is
       signaled. If all APs returned from Procedure prior to the timeout, then
       FailedCpuList is set to NULL. If not all APs return from Procedure before
       the timeout, then FailedCpuList is filled in with the list of the failed
       APs. The buffer is allocated by MP Service Protocol using AllocatePool().
       It is the caller's responsibility to free the buffer with FreePool() service.
    -# This invocation of SignalEvent() function informs the caller that invoked
       EFI_MP_SERVICES_PROTOCOL.StartupAllAPs() that either all the APs completed
       the specified task or a timeout occurred. The contents of FailedCpuList
       can be examined to determine which APs did not complete the specified task
       prior to the timeout.

  @param[in]  This                    A pointer to the EFI_MP_SERVICES_PROTOCOL
                                      instance.
  @param[in]  Procedure               A pointer to the function to be run on
                                      enabled APs of the system. See type
                                      EFI_AP_PROCEDURE.
  @param[in]  SingleThread            If TRUE, then all the enabled APs execute
                                      the function specified by Procedure one by
                                      one, in ascending order of processor handle
                                      number.  If FALSE, then all the enabled APs
                                      execute the function specified by Procedure
                                      simultaneously.
  @param[in]  WaitEvent               The event created by the caller with CreateEvent()
                                      service.  If it is NULL, then execute in
                                      blocking mode. BSP waits until all APs finish
                                      or TimeoutInMicroSeconds expires.  If it's
                                      not NULL, then execute in non-blocking mode.
                                      BSP requests the function specified by
                                      Procedure to be started on all the enabled
                                      APs, and go on executing immediately. If
                                      all return from Procedure, or TimeoutInMicroSeconds
                                      expires, this event is signaled. The BSP
                                      can use the CheckEvent() or WaitForEvent()
                                      services to check the state of event.  Type
                                      EFI_EVENT is defined in CreateEvent() in
                                      the Unified Extensible Firmware Interface
                                      Specification.
  @param[in]  TimeoutInMicrosecsond   Indicates the time limit in microseconds for
                                      APs to return from Procedure, either for
                                      blocking or non-blocking mode. Zero means
                                      infinity.  If the timeout expires before
                                      all APs return from Procedure, then Procedure
                                      on the failed APs is terminated. All enabled
                                      APs are available for next function assigned
                                      by EFI_MP_SERVICES_PROTOCOL.StartupAllAPs()
                                      or EFI_MP_SERVICES_PROTOCOL.StartupThisAP().
                                      If the timeout expires in blocking mode,
                                      BSP returns EFI_TIMEOUT.  If the timeout
                                      expires in non-blocking mode, WaitEvent
                                      is signaled with SignalEvent().
  @param[in]  ProcedureArgument       The parameter passed into Procedure for
                                      all APs.
  @param[out] FailedCpuList           If NULL, this parameter is ignored. Otherwise,
                                      if all APs finish successfully, then its
                                      content is set to NULL. If not all APs
                                      finish before timeout expires, then its
                                      content is set to address of the buffer
                                      holding handle numbers of the failed APs.
                                      The buffer is allocated by MP Service Protocol,
                                      and it's the caller's responsibility to
                                      free the buffer with FreePool() service.
                                      In blocking mode, it is ready for consumption
                                      when the call returns. In non-blocking mode,
                                      it is ready when WaitEvent is signaled.  The
                                      list of failed CPU is terminated by
                                      END_OF_CPU_LIST.

  @retval EFI_SUCCESS             In blocking mode, all APs have finished before
                                  the timeout expired.
  @retval EFI_SUCCESS             In non-blocking mode, function has been dispatched
                                  to all enabled APs.
  @retval EFI_UNSUPPORTED         A non-blocking mode request was made after the
                                  UEFI event EFI_EVENT_GROUP_READY_TO_BOOT was
                                  signaled.
  @retval EFI_DEVICE_ERROR        Caller processor is AP.
  @retval EFI_NOT_STARTED         No enabled APs exist in the system.
  @retval EFI_NOT_READY           Any enabled APs are busy.
  @retval EFI_TIMEOUT             In blocking mode, the timeout expired before
                                  all enabled APs have finished.
  @retval EFI_INVALID_PARAMETER   Procedure is NULL.

**/
EFI_STATUS
MpStartupAllAPs (
  IN  EFI_MP_SERVICES_PROTOCOL  *This,
  IN  EFI_AP_PROCEDURE          Procedure,
  IN  BOOLEAN                   SingleThread,
  IN  EFI_EVENT                 WaitEvent               OPTIONAL,
  IN  UINTN                     TimeoutInMicroSeconds,
  IN  VOID                      *ProcedureArgument      OPTIONAL,
  OUT UINTN                     **FailedCpuList         OPTIONAL
  )
{
  UINTN      Index;
  UINTN      EventIndex;
  UINTN      IAm;
  UINT32     EnabledArmCpus;
  EFI_EVENT  CompletionEvent;
  EFI_EVENT  *Event;
  EFI_STATUS Status;

  Status = MpWhoAmI (This, &IAm);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Before to start dispatching process on the secondary cores we must ensure they are not busy.
  // This loop also identifies the list of enabled secondary cores
  EnabledArmCpus = 0;
  for (Index = 0; Index < mArmProcessorTable->NumberOfEntries; Index++) {
    if (Index != IAm) {
      // Check the targeted core is enabled - we do not use MpGetProcessorInfo because this function
      // do not make the difference between ARM_SMC_ID_PSCI_AFFINITY_INFO_ON and
      // ARM_SMC_ID_PSCI_AFFINITY_INFO_ON_PENDING
      Status = WaitForSecondaryToBeEnabled (&mArmProcessorTable->ArmCpus[Index]);
      if (EFI_ERROR (Status)) {
        continue;
      }

      // Check if the core is not already running some code
      if (IsSecondaryCoreBusy (Index)) {
        return EFI_NOT_READY;
      } else {
        EnabledArmCpus |= (1 << Index);
      }
    }
  }

  if (EnabledArmCpus == 0) {
    return EFI_NOT_STARTED;
  }

  if (SingleThread) {
    // Completion Event
    Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, CompletionNotifyFunction, NULL, &CompletionEvent);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    for (Index = 0; Index < mArmProcessorTable->NumberOfEntries; Index++) {
      if (EnabledArmCpus & (1 << Index)) {
        Status = SetProcedureToSecondaryCore (Index, Procedure, ProcedureArgument, CompletionEvent);
        if (EFI_ERROR (Status)) {
          return Status;
        }

        // Send the interrupt to all cores
        ArmGicSendSgiTo (FixedPcdGet32 (PcdGicDistributorBase), ARM_GIC_ICDSGIR_FILTER_EVERYONEELSE, 0x0E, PcdGet32 (PcdGicSgiIntId));

        // Wait for the procedure to be completed on the secondary core before moving to the next one
        gBS->WaitForEvent (1, &CompletionEvent, &EventIndex);
      }
    }

    gBS->CloseEvent (CompletionEvent);

    if (WaitEvent) {
      gBS->SignalEvent (WaitEvent);
    }
  } else {
    if (WaitEvent == NULL) {
      // Completion Event
      Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, CompletionNotifyFunction, NULL, &CompletionEvent);
      if (EFI_ERROR (Status)) {
        return Status;
      }
      Event = &CompletionEvent;
    } else {
      Event = &WaitEvent;
    }

    // Set the Procedure all the secondary cores
    for (Index = 0; Index < mArmProcessorTable->NumberOfEntries; Index++) {
      if (EnabledArmCpus & (1 << Index)) {
        Status = SetProcedureToSecondaryCore (Index, Procedure, ProcedureArgument, *Event);
        if (EFI_ERROR (Status)) {
          return Status;
        }
      }
    }

    // Send the interrupt to all cores
    ArmGicSendSgiTo (FixedPcdGet32 (PcdGicDistributorBase), ARM_GIC_ICDSGIR_FILTER_EVERYONEELSE, 0x0E, PcdGet32 (PcdGicSgiIntId));

    // If we are in blocking mode
    if (WaitEvent == NULL) {
      gBS->WaitForEvent (1, Event, &EventIndex);
    }
  }

  return EFI_SUCCESS;
}

/**
  This service lets the caller get one enabled AP to execute a caller-provided
  function. The caller can request the BSP to either wait for the completion
  of the AP or just proceed with the next task by using the EFI event mechanism.
  See EFI_MP_SERVICES_PROTOCOL.StartupAllAPs() for more details on non-blocking
  execution support.  This service may only be called from the BSP.

  This function is used to dispatch one enabled AP to the function specified by
  Procedure passing in the argument specified by ProcedureArgument.  If WaitEvent
  is NULL, execution is in blocking mode. The BSP waits until the AP finishes or
  TimeoutInMicroSecondss expires. Otherwise, execution is in non-blocking mode.
  BSP proceeds to the next task without waiting for the AP. If a non-blocking mode
  is requested after the UEFI Event EFI_EVENT_GROUP_READY_TO_BOOT is signaled,
  then EFI_UNSUPPORTED must be returned.

  If the timeout specified by TimeoutInMicroseconds expires before the AP returns
  from Procedure, then execution of Procedure by the AP is terminated. The AP is
  available for subsequent calls to EFI_MP_SERVICES_PROTOCOL.StartupAllAPs() and
  EFI_MP_SERVICES_PROTOCOL.StartupThisAP().

  @param[in]  This                    A pointer to the EFI_MP_SERVICES_PROTOCOL
                                      instance.
  @param[in]  Procedure               A pointer to the function to be run on
                                      enabled APs of the system. See type
                                      EFI_AP_PROCEDURE.
  @param[in]  ProcessorNumber         The handle number of the AP. The range is
                                      from 0 to the total number of logical
                                      processors minus 1. The total number of
                                      logical processors can be retrieved by
                                      EFI_MP_SERVICES_PROTOCOL.GetNumberOfProcessors().
  @param[in]  WaitEvent               The event created by the caller with CreateEvent()
                                      service.  If it is NULL, then execute in
                                      blocking mode. BSP waits until all APs finish
                                      or TimeoutInMicroSeconds expires.  If it's
                                      not NULL, then execute in non-blocking mode.
                                      BSP requests the function specified by
                                      Procedure to be started on all the enabled
                                      APs, and go on executing immediately. If
                                      all return from Procedure or TimeoutInMicroSeconds
                                      expires, this event is signaled. The BSP
                                      can use the CheckEvent() or WaitForEvent()
                                      services to check the state of event.  Type
                                      EFI_EVENT is defined in CreateEvent() in
                                      the Unified Extensible Firmware Interface
                                      Specification.
  @param[in]  TimeoutInMicrosecsond   Indicates the time limit in microseconds for
                                      APs to return from Procedure, either for
                                      blocking or non-blocking mode. Zero means
                                      infinity.  If the timeout expires before
                                      all APs return from Procedure, then Procedure
                                      on the failed APs is terminated. All enabled
                                      APs are available for next function assigned
                                      by EFI_MP_SERVICES_PROTOCOL.StartupAllAPs()
                                      or EFI_MP_SERVICES_PROTOCOL.StartupThisAP().
                                      If the timeout expires in blocking mode,
                                      BSP returns EFI_TIMEOUT.  If the timeout
                                      expires in non-blocking mode, WaitEvent
                                      is signaled with SignalEvent().
  @param[in]  ProcedureArgument       The parameter passed into Procedure for
                                      all APs.
  @param[out] Finished                If NULL, this parameter is ignored.  In
                                      blocking mode, this parameter is ignored.
                                      In non-blocking mode, if AP returns from
                                      Procedure before the timeout expires, its
                                      content is set to TRUE. Otherwise, the
                                      value is set to FALSE. The caller can
                                      determine if the AP returned from Procedure
                                      by evaluating this value.

  @retval EFI_SUCCESS             In blocking mode, specified AP finished before
                                  the timeout expires.
  @retval EFI_SUCCESS             In non-blocking mode, the function has been
                                  dispatched to specified AP.
  @retval EFI_UNSUPPORTED         A non-blocking mode request was made after the
                                  UEFI event EFI_EVENT_GROUP_READY_TO_BOOT was
                                  signaled.
  @retval EFI_DEVICE_ERROR        The calling processor is an AP.
  @retval EFI_TIMEOUT             In blocking mode, the timeout expired before
                                  the specified AP has finished.
  @retval EFI_NOT_READY           The specified AP is busy.
  @retval EFI_NOT_FOUND           The processor with the handle specified by
                                  ProcessorNumber does not exist.
  @retval EFI_INVALID_PARAMETER   ProcessorNumber specifies the BSP or disabled AP.
  @retval EFI_INVALID_PARAMETER   Procedure is NULL.

**/
EFI_STATUS
MpStartupThisAP (
  IN  EFI_MP_SERVICES_PROTOCOL  *This,
  IN  EFI_AP_PROCEDURE          Procedure,
  IN  UINTN                     ProcessorNumber,
  IN  EFI_EVENT                 WaitEvent               OPTIONAL,
  IN  UINTN                     TimeoutInMicroseconds,
  IN  VOID                      *ProcedureArgument      OPTIONAL,
  OUT BOOLEAN                   *Finished               OPTIONAL
  )
{
  EFI_STATUS    Status;
  UINTN         IAm;
  ARM_CORE_INFO *ArmCpu;
  UINTN         EventIndex;

  if (ProcessorNumber > mArmProcessorTable->NumberOfEntries) {
    return EFI_NOT_FOUND;
  }

  Status = MpWhoAmI (This, &IAm);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (IAm == ProcessorNumber) {
    return EFI_INVALID_PARAMETER;
  }

  // Check the targeted core is enabled - we do not use MpGetProcessorInfo because this function
  // do not make the difference between ARM_SMC_ID_PSCI_AFFINITY_INFO_ON and ARM_SMC_ID_PSCI_AFFINITY_INFO_ON_PENDING
  ArmCpu = &mArmProcessorTable->ArmCpus[ProcessorNumber];
  Status = WaitForSecondaryToBeEnabled (ArmCpu);
  if (EFI_ERROR (Status)) {
    return EFI_DEVICE_ERROR;
  }

  // Check if the core is not already running some code
  if (IsSecondaryCoreBusy (ProcessorNumber)) {
    return EFI_NOT_READY;
  }

  // If it is blocking we need to create a completion event
  if (WaitEvent != NULL) {
    mMpProcessorInfo[ProcessorNumber].CompletionEvent = WaitEvent;
  } else {
    Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, CompletionNotifyFunction, NULL, &mMpProcessorInfo[ProcessorNumber].CompletionEvent);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = SetProcedureToSecondaryCore (ProcessorNumber, Procedure, ProcedureArgument, WaitEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Send the interrupt to all cores
  ArmGicSendSgiTo (FixedPcdGet32 (PcdGicDistributorBase), ARM_GIC_ICDSGIR_FILTER_EVERYONEELSE, 0x0E, PcdGet32 (PcdGicSgiIntId));

  // If it is a blocking request then we wait
  if (WaitEvent == NULL) {
    gBS->WaitForEvent (1, &mMpProcessorInfo[ProcessorNumber].CompletionEvent, &EventIndex);
    gBS->CloseEvent (mMpProcessorInfo[ProcessorNumber].CompletionEvent);
  }

  return EFI_SUCCESS;
}

/**
  This service switches the requested AP to be the BSP from that point onward.
  This service changes the BSP for all purposes.   This call can only be performed
  by the current BSP.

  This service switches the requested AP to be the BSP from that point onward.
  This service changes the BSP for all purposes. The new BSP can take over the
  execution of the old BSP and continue seamlessly from where the old one left
  off. This service may not be supported after the UEFI Event EFI_EVENT_GROUP_READY_TO_BOOT
  is signaled.

  If the BSP cannot be switched prior to the return from this service, then
  EFI_UNSUPPORTED must be returned.

  @param[in] This              A pointer to the EFI_MP_SERVICES_PROTOCOL instance.
  @param[in] ProcessorNumber   The handle number of AP that is to become the new
                               BSP. The range is from 0 to the total number of
                               logical processors minus 1. The total number of
                               logical processors can be retrieved by
                               EFI_MP_SERVICES_PROTOCOL.GetNumberOfProcessors().
  @param[in] EnableOldBSP      If TRUE, then the old BSP will be listed as an
                               enabled AP. Otherwise, it will be disabled.

  @retval EFI_SUCCESS             BSP successfully switched.
  @retval EFI_UNSUPPORTED         Switching the BSP cannot be completed prior to
                                  this service returning.
  @retval EFI_UNSUPPORTED         Switching the BSP is not supported.
  @retval EFI_SUCCESS             The calling processor is an AP.
  @retval EFI_NOT_FOUND           The processor with the handle specified by
                                  ProcessorNumber does not exist.
  @retval EFI_INVALID_PARAMETER   ProcessorNumber specifies the current BSP or
                                  a disabled AP.
  @retval EFI_NOT_READY           The specified AP is busy.

**/
EFI_STATUS
MpSwitchBSP (
  IN EFI_MP_SERVICES_PROTOCOL  *This,
  IN  UINTN                    ProcessorNumber,
  IN  BOOLEAN                  EnableOldBSP
  )
{
  ASSERT (0);
  return EFI_SUCCESS;
}

/**
  This service lets the caller enable or disable an AP from this point onward.
  This service may only be called from the BSP.

  This service allows the caller enable or disable an AP from this point onward.
  The caller can optionally specify the health status of the AP by Health. If
  an AP is being disabled, then the state of the disabled AP is implementation
  dependent. If an AP is enabled, then the implementation must guarantee that a
  complete initialization sequence is performed on the AP, so the AP is in a state
  that is compatible with an MP operating system. This service may not be supported
  after the UEFI Event EFI_EVENT_GROUP_READY_TO_BOOT is signaled.

  If the enable or disable AP operation cannot be completed prior to the return
  from this service, then EFI_UNSUPPORTED must be returned.

  @param[in] This              A pointer to the EFI_MP_SERVICES_PROTOCOL instance.
  @param[in] ProcessorNumber   The handle number of AP that is to become the new
                               BSP. The range is from 0 to the total number of
                               logical processors minus 1. The total number of
                               logical processors can be retrieved by
                               EFI_MP_SERVICES_PROTOCOL.GetNumberOfProcessors().
  @param[in] EnableAP          Specifies the new state for the processor for
                               enabled, FALSE for disabled.
  @param[in] HealthFlag        If not NULL, a pointer to a value that specifies
                               the new health status of the AP. This flag
                               corresponds to StatusFlag defined in
                               EFI_MP_SERVICES_PROTOCOL.GetProcessorInfo(). Only
                               the PROCESSOR_HEALTH_STATUS_BIT is used. All other
                               bits are ignored.  If it is NULL, this parameter
                               is ignored.

  @retval EFI_SUCCESS             The specified AP was enabled or disabled successfully.
  @retval EFI_UNSUPPORTED         Enabling or disabling an AP cannot be completed
                                  prior to this service returning.
  @retval EFI_UNSUPPORTED         Enabling or disabling an AP is not supported.
  @retval EFI_DEVICE_ERROR        The calling processor is an AP.
  @retval EFI_NOT_FOUND           Processor with the handle specified by ProcessorNumber
                                  does not exist.
  @retval EFI_INVALID_PARAMETER   ProcessorNumber specifies the BSP.

**/
EFI_STATUS
MpEnableDisableAP (
  IN  EFI_MP_SERVICES_PROTOCOL  *This,
  IN  UINTN                     ProcessorNumber,
  IN  BOOLEAN                   EnableAP,
  IN  UINT32                    *HealthFlag OPTIONAL
  )
{
  EFI_STATUS    Status;
  ARM_SMC_ARGS  SmcArgs;
  ARM_CORE_INFO *ArmCpu;
  UINTN         IAm;
  UINTN         MailboxCodeSize;

  if (ProcessorNumber > mArmProcessorTable->NumberOfEntries) {
    return EFI_NOT_FOUND;
  }

  ArmCpu = &mArmProcessorTable->ArmCpus[ProcessorNumber];

  Status = MpWhoAmI (This, &IAm);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (IAm == ProcessorNumber) {
    return EFI_INVALID_PARAMETER;
  }

  if (mMpProcessorInfo[ProcessorNumber].Mailbox == 0) {
    //TODO: We might want to allocate as Runtime Code/Reserved
    Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesCode, EFI_SIZE_TO_PAGES (ACPI_ARM_MP_MAILBOX_SIZE), &mMpProcessorInfo[ProcessorNumber].Mailbox);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    // Copy the parking algorithm into the mailbox
    MailboxCodeSize = (UINTN)MailboxCodeEnd - (UINTN)MailboxCodeStart;

    // In this case we split the firmware part of the mailbox into two regions: stack and code regions
    // Ensure the code fits into the code region
    ASSERT (MailboxCodeSize < (ACPI_ARM_MP_MAILBOX_FW_SIZE / 2));

    CopyMem ((VOID*)(UINTN)mMpProcessorInfo[ProcessorNumber].Mailbox, MailboxCodeStart, MailboxCodeSize);

    // Patch Data
    PATCH_MAILBOX_DATA(UINT32, ProcessorNumber, MailboxCodeStack, mMpProcessorInfo[ProcessorNumber].Mailbox + ACPI_ARM_MP_MAILBOX_FW_SIZE);
    PATCH_MAILBOX_DATA(UINT32, ProcessorNumber, MailboxCodeProcessorIdOffset, mMpProcessorInfo[ProcessorNumber].Mailbox + ACPI_ARM_MP_MAILBOX_CPU_ID_OFFSET);
    PATCH_MAILBOX_DATA(UINT64, ProcessorNumber, MailboxCodeJumpAddressOffset, mMpProcessorInfo[ProcessorNumber].Mailbox + ACPI_ARM_MP_MAILBOX_JUMP_ADDR_OFFSET);

    //
    // Map the page as Strongly-Ordered Memory
    //
    if (mCpu == NULL) {
      // Ensure the Cpu architectural protocol is already installed
      Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&mCpu);
      if (EFI_ERROR (Status)) {
        gBS->FreePages (mMpProcessorInfo[ProcessorNumber].Mailbox, EFI_SIZE_TO_PAGES (ACPI_ARM_MP_MAILBOX_SIZE));
        return Status;
      }
    }

    Status = mCpu->SetMemoryAttributes (mCpu, mMpProcessorInfo[ProcessorNumber].Mailbox, ACPI_ARM_MP_MAILBOX_SIZE, EFI_MEMORY_UC);
    if (EFI_ERROR (Status)) {
      gBS->FreePages (mMpProcessorInfo[ProcessorNumber].Mailbox, EFI_SIZE_TO_PAGES (ACPI_ARM_MP_MAILBOX_SIZE));
      return Status;
    }
  }

#ifdef MDE_CPU_AARCH64
  SmcArgs.Arg0 = ARM_SMC_ID_PSCI_CPU_ON_AARCH64;
  SmcArgs.Arg1 = ARM_SMC_PSCI_TARGET_CPU64 (0, 0, ArmCpu->ClusterId, ArmCpu->CoreId);
#else
  SmcArgs.Arg0 = ARM_SMC_ID_PSCI_CPU_ON_AARCH32;
  SmcArgs.Arg1 = ARM_SMC_PSCI_TARGET_CPU32 (0, ArmCpu->ClusterId, ArmCpu->CoreId);
#endif
  SmcArgs.Arg2 = (UINTN)mMpProcessorInfo[ProcessorNumber].Mailbox;
  SmcArgs.Arg3 = 0;

  ArmCallSmc (&SmcArgs);
  if ((SmcArgs.Arg0 == ARM_SMC_PSCI_RET_SUCCESS) || (SmcArgs.Arg0 == ARM_SMC_PSCI_RET_ALREADY_ON)) {
    return EFI_SUCCESS;
  } else {
    return EFI_UNSUPPORTED;
  }
}

EFI_MP_SERVICES_PROTOCOL mMpServicesInstance = {
  MpGetNumberOfProcessors,
  MpGetProcessorInfo,
  MpStartupAllAPs,
  MpStartupThisAP,
  MpSwitchBSP,
  MpEnableDisableAP,
  MpWhoAmI
};

EFI_STATUS
ArmMpServicesAcpiPsciInit (
  VOID
  )
{
  EFI_STATUS Status;
  EFI_HANDLE Handle;
  UINTN      Index;

  // Look for MP Core Info Table
  mArmProcessorTable = NULL;
  for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    // Check for correct GUID type
    if (CompareGuid (&gArmMpCoreInfoGuid, &(gST->ConfigurationTable[Index].VendorGuid))) {
      mArmProcessorTable = (ARM_PROCESSOR_TABLE *)gST->ConfigurationTable[Index].VendorTable;
    }
  }

  if (mArmProcessorTable == NULL) {
    return EFI_NOT_FOUND;
  }

  // Reserve the table for the mailbox addresses
  mMpProcessorInfo = AllocateZeroPool (mArmProcessorTable->NumberOfEntries * sizeof (PROCESSOR_INFO));
  if (mMpProcessorInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiMpServiceProtocolGuid, &mMpServicesInstance,
                  NULL
                  );

  return Status;
}
