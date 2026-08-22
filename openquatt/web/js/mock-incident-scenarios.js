(function () {
  const clone = (value) => {
    if (typeof structuredClone === "function") {
      return structuredClone(value);
    }
    return JSON.parse(JSON.stringify(value));
  };

  const deepFreeze = (value) => {
    if (!value || typeof value !== "object" || Object.isFrozen(value)) {
      return value;
    }
    Object.values(value).forEach(deepFreeze);
    return Object.freeze(value);
  };

  const system = (overrides = {}) => ({
    control_mode: 2,
    action: "none",
    boiler_role: "off",
    previous_boiler_role: "off",
    boiler_command_active: false,
    boiler_output_continuous: false,
    fallback_block_reason: 0,
    ...overrides,
  });

  const heatPump = (index, overrides = {}) => {
    const running = overrides.run_state === "running";
    return {
      index,
      link_state: "healthy",
      protection_state: "clear",
      run_state: running ? "running" : "stopped",
      availability: "available",
      available_for_start: true,
      must_stop: false,
      fault_active: false,
      protection_active: false,
      running_confirmed: running,
      stop_confirmed: !running,
      stop_confirmation_pending: false,
      stop_unconfirmed: false,
      fallback_cause_present: false,
      fallback_eligible: false,
      primary_incident_id: 0,
      last_action_result: null,
      incidents: [],
      ...overrides,
    };
  };

  const runtime = (overrides = {}) => ({
    lifecycle: "active",
    raw_active: true,
    confirmed_active: true,
    latched: false,
    acknowledged: false,
    first_seen_ms: 1000,
    last_seen_ms: 1000,
    cleared_at_ms: 0,
    first_seen_s: 0,
    last_seen_s: 0,
    cleared_at_s: 0,
    occurrence_count: 1,
    ...overrides,
  });

  const incident = ({
    id,
    key,
    presentationKey,
    category,
    severity,
    effects,
    effectMask,
    userAction,
    recoveryCondition,
    registerAddress = 0,
    bit = 0,
  }, runtimeOverrides = {}) => ({
    definition: {
      id,
      key,
      presentation_key: presentationKey,
      category,
      severity,
      effects,
      effect_mask: effectMask,
      user_action: userAction,
      recovery_condition: recoveryCondition,
      register_address: registerAddress,
      bit,
    },
    runtime: runtime(runtimeOverrides),
  });

  const INCIDENTS = {
    oilReturn: (runtimeOverrides = {}) => incident({
      id: 4,
      key: "compressor_oil_return",
      presentationKey: "hp.compressor_oil_return_status",
      category: "status",
      severity: "info",
      effects: ["display"],
      effectMask: 1,
      userAction: "none",
      recoveryCondition: "when_bit_clears",
      registerAddress: 2119,
      bit: 3,
    }, runtimeOverrides),
    pressureLimit: (runtimeOverrides = {}) => incident({
      id: 6,
      key: "high_pressure_speed_limit",
      presentationKey: "hp.high_pressure_speed_limit",
      category: "protection",
      severity: "warning",
      effects: ["display", "limit_capacity"],
      effectMask: 3,
      userAction: "wait_for_automatic_recovery",
      recoveryCondition: "after_stable_reads",
      registerAddress: 2119,
      bit: 5,
    }, runtimeOverrides),
    condenserSensor: (runtimeOverrides = {}) => incident({
      id: 22,
      key: "condenser_pressure_sensor",
      presentationKey: "hp.condenser_pressure_sensor_fault",
      category: "fault",
      severity: "fault",
      effects: ["display", "block_start", "stop_compressor", "mark_hp_unavailable", "allow_cm4"],
      effectMask: 93,
      userAction: "contact_installer",
      recoveryCondition: "stable_reads_and_recovery_window",
      registerAddress: 2120,
      bit: 5,
    }, runtimeOverrides),
    powerCycle: (runtimeOverrides = {}) => incident({
      id: 21,
      key: "evaporator_pressure_sensor_lock",
      presentationKey: "hp.evaporator_pressure_sensor_lock",
      category: "fault",
      severity: "fault",
      effects: [
        "display",
        "block_start",
        "stop_compressor",
        "mark_hp_unavailable",
        "allow_cm4",
        "require_confirmed_odu_power_cycle",
      ],
      effectMask: 349,
      userAction: "contact_installer",
      recoveryCondition: "confirmed_odu_power_cycle",
      registerAddress: 2120,
      bit: 4,
    }, runtimeOverrides),
    linkLoss: (runtimeOverrides = {}) => incident({
      id: 1001,
      key: "hp_link_loss",
      presentationKey: "hp.link_loss",
      category: "fault",
      severity: "fault",
      effects: ["display", "block_start", "stop_compressor", "mark_hp_unavailable", "allow_cm4"],
      effectMask: 93,
      userAction: "check_installation",
      recoveryCondition: "stable_telemetry",
    }, runtimeOverrides),
    startFailed: (runtimeOverrides = {}) => incident({
      id: 1002,
      key: "hp_start_failed",
      presentationKey: "hp.start_failed",
      category: "fault",
      severity: "fault",
      effects: ["display", "block_start", "stop_compressor", "mark_hp_unavailable", "allow_cm4"],
      effectMask: 93,
      userAction: "check_installation",
      recoveryCondition: "explicit_retry_after_safe_stop",
    }, runtimeOverrides),
    stopUnconfirmed: (runtimeOverrides = {}) => incident({
      id: 1003,
      key: "hp_stop_unconfirmed",
      presentationKey: "hp.stop_unconfirmed",
      category: "fault",
      severity: "fault",
      effects: ["display", "block_start", "stop_compressor", "mark_hp_unavailable", "block_boiler"],
      effectMask: 157,
      userAction: "check_installation",
      recoveryCondition: "fresh_stop_confirmation",
    }, runtimeOverrides),
  };

  const event = (
    atS,
    eventType,
    subject,
    reason,
    severity,
    cm,
    from,
    to,
    valueA = 0,
    valueB = 0,
    thresholdA = 0,
    durationS = 0,
    flags = 0,
  ) => ({
    at_s: atS,
    event_type: eventType,
    subject,
    reason,
    severity,
    cm,
    from,
    to,
    value_a: valueA,
    value_b: valueB,
    threshold_a: thresholdA,
    duration_s: durationS,
    flags,
  });

  const phase = (id, label, description, elapsedS, options = {}) => ({
    id,
    label,
    description,
    elapsed_s: elapsedS,
    incident_http_status: 200,
    system: system(options.system),
    heat_pumps: options.heatPumps || [],
    events: options.events || [],
    actions: options.actions || {},
    entity_patch: options.entityPatch || {},
    ...options.phaseOverrides,
  });

  const scenario = (id, label, group, topology, phases, options = {}) => ({
    id,
    label,
    group,
    topology,
    phases,
    base_scenario: options.baseScenario || "heating",
    boiler_transport: options.boilerTransport || "",
    required_hardware: options.requiredHardware || "",
  });

  const hp1Running = () => heatPump(1, { run_state: "running" });
  const hp2Standby = () => heatPump(2);
  const stoppedFaulted = (index, primaryIncidentId, incidents, overrides = {}) => heatPump(index, {
    protection_state: "fault_active",
    availability: "unavailable",
    available_for_start: false,
    must_stop: true,
    fault_active: true,
    fallback_cause_present: true,
    fallback_eligible: true,
    primary_incident_id: primaryIncidentId,
    incidents,
    ...overrides,
  });

  const scenarios = [
    scenario("none", "Geen warmtepompstoring", "Basis", "any", [
      phase("normal", "Normaal bedrijf", "Geen actief incident; de normale bedrijfssituatie bepaalt de telemetrie.", 0, {
        heatPumps: [hp1Running()],
      }),
    ]),

    scenario("brief-link-dip", "Korte communicatiedip", "Communicatie", "any", [
      phase("healthy", "Verbinding gezond", "HP1 draait met een geldige verbinding.", 0, {
        heatPumps: [hp1Running()],
      }),
      phase("suspect-1", "Eerste onvolledige ronde", "De link is verdacht; de draaiende HP blijft doorlopen en er volgt nog geen stop.", 10, {
        heatPumps: [heatPump(1, {
          link_state: "suspect",
          run_state: "running",
          availability: "unknown",
          available_for_start: false,
          running_confirmed: true,
          stop_confirmed: false,
        })],
      }),
      phase("suspect-2", "Tweede onvolledige ronde", "Nog steeds geen bevestigde uitval: geen stop en geen CM4.", 20, {
        heatPumps: [heatPump(1, {
          link_state: "suspect",
          run_state: "running",
          availability: "unknown",
          available_for_start: false,
          running_confirmed: true,
          stop_confirmed: false,
        })],
      }),
      phase("recovered", "Verbinding hersteld", "De korte dip herstelt zonder incident of regelactie.", 25, {
        heatPumps: [hp1Running()],
      }),
    ]),

    scenario("confirmed-link-loss", "Bevestigd linkverlies → CM4", "Communicatie", "single", [
      phase("healthy", "Verbinding gezond", "HP1 draait normaal.", 0, {
        heatPumps: [hp1Running()],
      }),
      phase("suspect", "Uitval wordt gefilterd", "De debounce loopt; de draaiende HP wordt nog niet gestopt.", 20, {
        heatPumps: [heatPump(1, {
          link_state: "suspect",
          run_state: "running",
          availability: "unknown",
          available_for_start: false,
          running_confirmed: true,
          stop_confirmed: false,
        })],
      }),
      phase("lost", "Linkverlies bevestigd", "Na 30 seconden en drie onvolledige rondes is de link werkelijk weg.", 30, {
        system: {
          control_mode: 1,
          action: "fallback_blocked",
          fallback_block_reason: 10,
        },
        heatPumps: [stoppedFaulted(1, 1001, [INCIDENTS.linkLoss()], {
          link_state: "lost",
          protection_state: "clear",
          run_state: "stopping",
          stop_confirmed: false,
          stop_confirmation_pending: true,
          fault_active: false,
          fallback_eligible: false,
        })],
        events: [
          event(30, "incident_start", "HP1", "hp_link_loss", "fault", 1, "standby", "active", 1001),
          event(30, "hp_availability_change", "HP1", "hp_link_loss", "fault", 1, "available", "offline", 1001, 2),
        ],
      }),
      phase("fallback", "Stop bevestigd, CM4 actief", "De stopstatus is vers bevestigd; de ketel neemt veilig over.", 36, {
        system: {
          control_mode: 4,
          action: "boiler_fallback",
          boiler_role: "fallback",
          previous_boiler_role: "off",
          boiler_command_active: true,
        },
        heatPumps: [stoppedFaulted(1, 1001, [INCIDENTS.linkLoss()], {
          link_state: "lost",
          protection_state: "clear",
          fault_active: false,
        })],
        events: [
          event(34, "hp_stop_confirmed", "HP1", "hp_link_loss", "normal", 1, "active", "standby"),
          event(36, "control_mode_change", "SYSTEM", "boiler_fallback", "attention", 4, "standby", "active", 1, 4),
          event(36, "boiler_fallback_start", "CV", "boiler_fallback", "attention", 4, "standby", "active", 350, 0),
        ],
      }),
    ], { boilerTransport: "R1" }),

    scenario("status-only", "Statusmelding: olie-retour", "Incidentmodel", "any", [
      phase("active", "Status actief", "De melding is zichtbaar, maar begrenst of stopt de warmtepomp niet.", 5, {
        heatPumps: [heatPump(1, {
          run_state: "running",
          running_confirmed: true,
          stop_confirmed: false,
          primary_incident_id: 4,
          incidents: [INCIDENTS.oilReturn()],
        })],
        events: [
          event(5, "incident_start", "HP1", "hp_protection", "normal", 2, "standby", "active", 4),
        ],
      }),
    ]),

    scenario("capacity-protection", "Protection: vermogen begrensd", "Incidentmodel", "any", [
      phase("active", "Begrenzing actief", "De ODU begrenst het vermogen; HP1 blijft inzetbaar.", 5, {
        heatPumps: [heatPump(1, {
          protection_state: "limited",
          run_state: "running",
          protection_active: true,
          running_confirmed: true,
          stop_confirmed: false,
          primary_incident_id: 6,
          incidents: [INCIDENTS.pressureLimit()],
        })],
        events: [
          event(5, "incident_start", "HP1", "hp_protection", "limited", 2, "standby", "active", 6),
        ],
      }),
      phase("clear", "Begrenzing hersteld", "Meerdere stabiele metingen hebben de protection vrijgegeven.", 20, {
        heatPumps: [hp1Running()],
        events: [
          event(20, "incident_clear", "HP1", "hp_protection", "normal", 2, "active", "standby", 6),
        ],
      }),
    ]),

    scenario("hard-fault-recovery", "Harde ODU-fout → herstel", "Incidentmodel", "single", [
      phase("fault", "ODU-fout bevestigd", "HP1 is veilig gestopt en CM4 neemt de warmtevraag over.", 8, {
        system: {
          control_mode: 4,
          action: "boiler_fallback",
          boiler_role: "fallback",
          boiler_command_active: true,
        },
        heatPumps: [stoppedFaulted(1, 22, [INCIDENTS.condenserSensor()])],
        events: [
          event(4, "incident_start", "HP1", "hp_fault", "fault", 2, "standby", "active", 22),
          event(5, "hp_availability_change", "HP1", "hp_fault", "fault", 1, "available", "faulted", 22, 1),
          event(6, "hp_stop_confirmed", "HP1", "hp_fault", "normal", 1, "active", "standby"),
          event(8, "control_mode_change", "SYSTEM", "boiler_fallback", "attention", 4, "standby", "active", 1, 4),
          event(8, "boiler_fallback_start", "CV", "boiler_fallback", "attention", 4, "standby", "active", 350, 0),
        ],
        entityPatch: {
          hp1Failure: "Condenser pressure sensor failure",
        },
      }),
      phase("recovering", "Herstel wordt bevestigd", "De foutbit is weg, maar het stabiele herstelvenster loopt nog.", 38, {
        system: {
          control_mode: 4,
          action: "boiler_fallback",
          boiler_role: "fallback",
          previous_boiler_role: "fallback",
          boiler_command_active: true,
        },
        heatPumps: [heatPump(1, {
          protection_state: "fault_recovery",
          availability: "recovering",
          available_for_start: false,
          fallback_cause_present: true,
          fallback_eligible: true,
        })],
        events: [
          event(38, "incident_clear", "HP1", "hp_fault", "normal", 4, "active", "standby", 22),
          event(38, "hp_availability_change", "HP1", "hp_recovery_wait", "normal", 4, "faulted", "recovering", 22),
        ],
      }),
      phase("available", "HP1 stabiel hersteld", "HP1 is weer beschikbaar en CM4 wordt gecontroleerd beëindigd.", 98, {
        heatPumps: [hp1Running()],
        events: [
          event(98, "hp_availability_change", "HP1", "hp_recovered", "normal", 4, "recovering", "available"),
          event(98, "boiler_fallback_stop", "CV", "hp_recovered", "normal", 2, "active", "standby", 350, 0),
          event(98, "control_mode_change", "SYSTEM", "hp_recovered", "normal", 2, "active", "standby", 4, 2),
        ],
      }),
    ], { boilerTransport: "R1" }),

    scenario("start-failed-retry", "Start mislukt → expliciete retry", "Herstelacties", "single", [
      phase("failed", "Start niet bevestigd", "De startfout is actief; een retry mag pas na de bevestigde stop.", 12, {
        system: {
          control_mode: 4,
          action: "boiler_fallback",
          boiler_role: "fallback",
          boiler_command_active: true,
        },
        heatPumps: [stoppedFaulted(1, 1002, [INCIDENTS.startFailed()])],
        events: [
          event(8, "incident_start", "HP1", "hp_start_failed", "fault", 1, "standby", "active", 1002),
          event(8, "hp_availability_change", "HP1", "hp_start_failed", "fault", 1, "available", "faulted", 1002, 1),
          event(12, "boiler_fallback_start", "CV", "boiler_fallback", "attention", 4, "standby", "active", 350, 0),
        ],
        actions: {
          start_failure_retry: {
            target_phase: "retried",
            complete_after_reads: 2,
            ok: true,
            result: "start_failure_cleared",
          },
        },
      }),
      phase("retried", "Retry verwerkt", "De blokkering is vrijgegeven; HP1 is opnieuw gestart en CM4 stopt.", 18, {
        heatPumps: [hp1Running()],
        events: [
          event(16, "incident_clear", "HP1", "hp_start_failed", "normal", 4, "active", "standby", 1002),
          event(17, "hp_availability_change", "HP1", "hp_recovered", "normal", 4, "faulted", "available"),
          event(18, "hp_start_confirmed", "HP1", "hp_recovered", "normal", 2, "standby", "active"),
          event(18, "boiler_fallback_stop", "CV", "hp_recovered", "normal", 2, "active", "standby", 350, 0),
        ],
      }),
    ], { boilerTransport: "R1" }),

    scenario("power-cycle-confirmation", "Gelatchte fout → ODU-powercycle", "Herstelacties", "single", [
      phase("active", "Powercycle-fout actief", "De ODU-fout is actief en houdt HP1 buiten bedrijf.", 8, {
        system: {
          control_mode: 4,
          action: "boiler_fallback",
          boiler_role: "fallback",
          boiler_command_active: true,
        },
        heatPumps: [stoppedFaulted(1, 21, [INCIDENTS.powerCycle()])],
        events: [
          event(5, "incident_start", "HP1", "hp_fault", "fault", 1, "standby", "active", 21),
          event(8, "boiler_fallback_start", "CV", "boiler_fallback", "attention", 4, "standby", "active", 350, 0),
        ],
      }),
      phase("latched", "Oorzaak weg, latch blijft", "Na de echte ODU-powercycle kan de gebruiker deze herstelde latch bevestigen.", 24, {
        system: {
          control_mode: 4,
          action: "boiler_fallback",
          boiler_role: "fallback",
          previous_boiler_role: "fallback",
          boiler_command_active: true,
        },
        heatPumps: [stoppedFaulted(1, 21, [INCIDENTS.powerCycle({
          lifecycle: "latched",
          raw_active: false,
          confirmed_active: false,
          latched: true,
          cleared_at_ms: 24000,
        })])],
        events: [
          event(24, "incident_clear", "HP1", "hp_fault", "normal", 4, "active", "standby", 21, 0, 0, 0, 1),
        ],
        actions: {
          confirm_odu_power_cycle: {
            target_phase: "confirmed",
            complete_after_reads: 2,
            reject_csrf_once: true,
            ok: true,
            result: "odu_power_cycle_confirmed",
          },
        },
      }),
      phase("confirmed", "Powercycle bevestigd", "De latch is opgeslagen en HP1 is weer inzetbaar.", 30, {
        heatPumps: [hp1Running()],
        events: [
          event(28, "incident_acknowledged", "HP1", "hp_fault", "normal", 4, "active", "standby", 21),
          event(30, "hp_availability_change", "HP1", "hp_recovered", "normal", 4, "faulted", "available"),
          event(30, "boiler_fallback_stop", "CV", "hp_recovered", "normal", 2, "active", "standby", 350, 0),
        ],
      }),
    ], { boilerTransport: "R1" }),

    scenario("cm3-cm4-r1", "CM3 → CM4 zonder R1-puls", "Ketelcontinuïteit", "single", [
      phase("assist", "CM3: CV ondersteunt", "De keteluitgang is al actief voor ondersteuning.", 0, {
        system: {
          control_mode: 3,
          action: "boiler_assist",
          boiler_role: "assist",
          boiler_command_active: true,
        },
        heatPumps: [hp1Running()],
      }),
      phase("fallback", "CM4: ketel neemt over", "Alleen de regelrol wijzigt; de fysieke R1-uitgang blijft actief.", 8, {
        system: {
          control_mode: 4,
          action: "boiler_fallback",
          boiler_role: "fallback",
          previous_boiler_role: "assist",
          boiler_command_active: true,
          boiler_output_continuous: true,
        },
        heatPumps: [stoppedFaulted(1, 22, [INCIDENTS.condenserSensor()])],
        events: [
          event(3, "incident_start", "HP1", "hp_fault", "fault", 3, "standby", "active", 22),
          event(5, "hp_stop_confirmed", "HP1", "hp_fault", "normal", 3, "active", "standby"),
          event(8, "control_mode_change", "SYSTEM", "boiler_fallback", "attention", 4, "active", "active", 3, 4),
          event(8, "boiler_fallback_start", "CV", "boiler_fallback", "attention", 4, "active", "active", 350, 1),
        ],
      }),
    ], { boilerTransport: "R1" }),

    scenario("cm3-cm4-opentherm", "CM3 → CM4 zonder OpenTherm-onderbreking", "Ketelcontinuïteit", "single", [
      phase("assist", "CM3: OpenTherm ondersteunt", "De OpenTherm-warmteopdracht is actief.", 0, {
        system: {
          control_mode: 3,
          action: "boiler_assist",
          boiler_role: "assist",
          boiler_command_active: true,
        },
        heatPumps: [hp1Running()],
      }),
      phase("fallback", "CM4: OpenTherm neemt over", "De warmteopdracht blijft actief terwijl alleen de ketelrol verandert.", 8, {
        system: {
          control_mode: 4,
          action: "boiler_fallback",
          boiler_role: "fallback",
          previous_boiler_role: "assist",
          boiler_command_active: true,
          boiler_output_continuous: true,
        },
        heatPumps: [stoppedFaulted(1, 22, [INCIDENTS.condenserSensor()])],
        events: [
          event(3, "incident_start", "HP1", "hp_fault", "fault", 3, "standby", "active", 22),
          event(5, "hp_stop_confirmed", "HP1", "hp_fault", "normal", 3, "active", "standby"),
          event(8, "control_mode_change", "SYSTEM", "boiler_fallback", "attention", 4, "active", "active", 3, 4),
          event(8, "boiler_fallback_start", "CV", "boiler_fallback", "attention", 4, "active", "active", 350, 1),
        ],
      }),
    ], {
      boilerTransport: "OpenTherm",
      requiredHardware: "heatpump_controller_q",
    }),

    scenario("stop-unconfirmed-block", "CM4 geblokkeerd: stop onbevestigd", "Fallbackblokkades", "single", [
      phase("blocked", "Stopstatus ontbreekt", "De HP-fout is bevestigd, maar CM4 wacht op een verse veilige stopstatus.", 12, {
        system: {
          control_mode: 3,
          action: "fallback_blocked",
          boiler_role: "assist",
          previous_boiler_role: "assist",
          boiler_command_active: true,
          fallback_block_reason: 10,
        },
        heatPumps: [stoppedFaulted(1, 22, [
          INCIDENTS.condenserSensor(),
          INCIDENTS.stopUnconfirmed(),
        ], {
          run_state: "stop_unconfirmed",
          stop_confirmed: false,
          stop_unconfirmed: true,
          fallback_eligible: false,
        })],
        events: [
          event(8, "incident_start", "HP1", "hp_stop_unconfirmed", "fault", 3, "standby", "active", 1003),
          event(12, "decision_blocked", "SYSTEM", "fallback_blocked", "fault", 3, "active", "blocked", 10),
        ],
      }),
    ], { boilerTransport: "R1" }),

    scenario("flow-block", "CM4 geblokkeerd: flow onvoldoende", "Fallbackblokkades", "single", [
      phase("blocked", "Flowbeveiliging blokkeert", "De HP is veilig gestopt, maar de actuele waterflow geeft CM4 niet vrij.", 12, {
        system: {
          control_mode: 1,
          action: "fallback_blocked",
          fallback_block_reason: 12,
        },
        heatPumps: [stoppedFaulted(1, 22, [INCIDENTS.condenserSensor()])],
        events: [
          event(6, "incident_start", "HP1", "hp_fault", "fault", 1, "standby", "active", 22),
          event(8, "hp_stop_confirmed", "HP1", "hp_fault", "normal", 1, "active", "standby"),
          event(12, "decision_blocked", "SYSTEM", "fallback_blocked", "fault", 1, "standby", "blocked", 12),
        ],
        entityPatch: {
          flowLph: 140,
        },
      }),
    ], { boilerTransport: "R1" }),

    scenario("duo-one-fault", "Duo: één HP uitgevallen", "Duo", "duo", [
      phase("hp1-fault", "HP1 uit, HP2 blijft draaien", "De gezonde HP2 blijft beschikbaar; ketelfallback wordt niet gevraagd.", 10, {
        heatPumps: [
          stoppedFaulted(1, 22, [INCIDENTS.condenserSensor()]),
          heatPump(2, { run_state: "running" }),
        ],
        events: [
          event(5, "incident_start", "HP1", "hp_fault", "fault", 2, "standby", "active", 22),
          event(6, "hp_availability_change", "HP1", "hp_fault", "fault", 2, "available", "faulted", 22, 1),
          event(8, "hp_stop_confirmed", "HP1", "hp_fault", "normal", 2, "active", "standby"),
        ],
      }),
    ]),

    scenario("duo-both-fault", "Duo: beide HP’s uit → CM4", "Duo", "duo", [
      phase("hp1-fault", "Alleen HP1 uitgevallen", "HP2 blijft de warmtevraag leveren; CM4 blijft uit.", 8, {
        heatPumps: [
          stoppedFaulted(1, 22, [INCIDENTS.condenserSensor()]),
          heatPump(2, { run_state: "running" }),
        ],
        events: [
          event(5, "incident_start", "HP1", "hp_fault", "fault", 2, "standby", "active", 22),
          event(8, "hp_stop_confirmed", "HP1", "hp_fault", "normal", 2, "active", "standby"),
        ],
      }),
      phase("both-fault", "Ook HP2 uitgevallen", "Beide HP’s zijn veilig gestopt; de ketel neemt de warmtevraag over.", 18, {
        system: {
          control_mode: 4,
          action: "boiler_fallback",
          boiler_role: "fallback",
          boiler_command_active: true,
        },
        heatPumps: [
          stoppedFaulted(1, 22, [INCIDENTS.condenserSensor()]),
          stoppedFaulted(2, 37, [incident({
            id: 37,
            key: "compressor_driver",
            presentationKey: "hp.compressor_driver_fault",
            category: "fault",
            severity: "fault",
            effects: ["display", "block_start", "stop_compressor", "mark_hp_unavailable", "allow_cm4"],
            effectMask: 93,
            userAction: "contact_installer",
            recoveryCondition: "stable_reads_and_recovery_window",
            registerAddress: 2121,
            bit: 4,
          })]),
        ],
        events: [
          event(12, "incident_start", "HP2", "hp_fault", "fault", 2, "standby", "active", 37),
          event(14, "hp_availability_change", "HP2", "hp_fault", "fault", 1, "available", "faulted", 37, 1),
          event(15, "hp_stop_confirmed", "HP2", "hp_fault", "normal", 1, "active", "standby"),
          event(18, "control_mode_change", "SYSTEM", "boiler_fallback", "attention", 4, "standby", "active", 1, 4),
          event(18, "boiler_fallback_start", "CV", "boiler_fallback", "attention", 4, "standby", "active", 350, 0),
        ],
      }),
    ], { boilerTransport: "R1" }),

    scenario("incident-api-transient", "Incident-API tijdelijk onbereikbaar", "API-fouten", "any", [
      phase("last-good", "Laatste goede snapshot", "De UI heeft een geldige, lege incidentsnapshot ontvangen.", 0, {
        heatPumps: [hp1Running()],
      }),
      phase("failure-1", "Eerste HTTP-fout", "De UI houdt de laatste goede snapshot zonder waarschuwing vast.", 5, {
        heatPumps: [hp1Running()],
        phaseOverrides: { incident_http_status: 503 },
      }),
      phase("failure-2", "Tweede HTTP-fout", "De tweede fout blijft binnen de tolerantiedrempel.", 10, {
        heatPumps: [hp1Running()],
        phaseOverrides: { incident_http_status: 503 },
      }),
      phase("failure-3", "Derde HTTP-fout", "Na de derde fout markeert de UI de incidentinformatie als verouderd.", 15, {
        heatPumps: [hp1Running()],
        phaseOverrides: { incident_http_status: 503 },
      }),
      phase("recovered", "API hersteld", "Een geldige snapshot maakt de installatiebewaking weer actueel.", 20, {
        heatPumps: [hp1Running()],
      }),
    ]),
  ];

  const scenarioById = new Map(scenarios.map((item) => [item.id, item]));

  function getScenario(id) {
    return scenarioById.get(String(id || "")) || scenarioById.get("none");
  }

  function isCompatible(item, installation) {
    return item.topology === "any" || item.topology === installation;
  }

  function getPhase(itemOrId, phaseIndex = 0) {
    const item = typeof itemOrId === "string" ? getScenario(itemOrId) : itemOrId;
    const index = Math.max(0, Math.min(item.phases.length - 1, Number(phaseIndex) || 0));
    return { phase: item.phases[index], index };
  }

  function buildPhaseState(id, phaseIndex, installation) {
    const item = getScenario(id);
    const { phase: selectedPhase, index } = getPhase(item, phaseIndex);
    const built = clone(selectedPhase);
    const present = new Set(built.heat_pumps.map((hp) => hp.index));
    if (!present.has(1)) {
      built.heat_pumps.push(hp1Running());
    }
    if (installation === "duo" && !present.has(2)) {
      built.heat_pumps.push(hp2Standby());
    }
    built.heat_pumps = built.heat_pumps
      .filter((hp) => hp.index === 1 || installation === "duo")
      .sort((left, right) => left.index - right.index);
    return {
      scenario: item,
      phase: built,
      phaseIndex: index,
    };
  }

  function collectEvents(id, phaseIndex) {
    const item = getScenario(id);
    const { index } = getPhase(item, phaseIndex);
    return item.phases
      .slice(0, index + 1)
      .flatMap((itemPhase) => itemPhase.events)
      .map(clone);
  }

  window.__OQ_MOCK_INCIDENT_SCENARIOS__ = deepFreeze({
    scenarios,
    getScenario,
    getPhase,
    isCompatible,
    buildPhaseState,
    collectEvents,
  });
})();
