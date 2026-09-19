import time
import math
import random
import threading
from datetime import datetime, timezone
from flask import current_app
from extensions import db, socketio
from models import Patient, Device, Telemetry
from risk_engine import RiskEngine
from alert_engine import AlertEngine

class PatientSimulator(threading.Thread):
    def __init__(self, app, patient_id):
        super().__init__()
        self.app = app
        self.patient_id = patient_id
        self.daemon = True
        self.running = True
        self.scenario = "DEFAULT"  # "DEFAULT", "NORMAL", "MODERATE", "HIGH_RISK"
        self.tick_count = 0

    def set_scenario(self, scenario):
        self.scenario = scenario

    def run(self):
        with self.app.app_context():
            patient = db.session.get(Patient, self.patient_id)
            if not patient:
                return
            
            # Identify patient baseline risk profile
            patient_code = patient.patient_code
            
        print(f"[Simulator] Started background telemetry stream for Patient ID {self.patient_id} ({patient_code})")

        while self.running:
            try:
                time.sleep(3.0)
                self.tick_count += 1

                with self.app.app_context():
                    patient = db.session.get(Patient, self.patient_id)
                    # Check if real hardware ESP32 telemetry was received recently for this patient
                    latest_real = Telemetry.query.filter_by(
                        patient_id=self.patient_id,
                        is_simulated=False
                    ).order_by(Telemetry.timestamp.desc()).first()

                    if latest_real:
                        ts = latest_real.timestamp
                        if ts.tzinfo is None:
                            ts = ts.replace(tzinfo=timezone.utc)
                        if (datetime.now(timezone.utc) - ts).total_seconds() < 10.0:
                            # Real ESP32 hardware telemetry active for this patient. Skip simulated tick.
                            continue

                    # Generate plausible dynamic values based on patient profile + scenario
                    t = self.tick_count * 0.2
                    sine_wave = math.sin(t)
                    noise = random.uniform(-0.8, 0.8)

                    base_p = dict(patient.baseline_pressure)
                    
                    if self.scenario == "HIGH_RISK" or (self.scenario == "DEFAULT" and patient.patient_code == "PT-8021"):
                        # High risk profile (Sita Devi): Right Forefoot (R_met) focal pressure spike & 34.6°C elevated temp
                        temp_r = round(34.6 + sine_wave * 0.3 + noise * 0.1, 1)
                        temp_l = temp_r
                        asym = round(18.0 + sine_wave * 1.5 + noise, 1)
                        base_p['R_met'] = round(82.0 + sine_wave * 4.0 + noise * 2.0, 1)
                        base_p['R_toe'] = round(55.0 + sine_wave * 2.0, 1)
                    
                    elif self.scenario == "MODERATE" or (self.scenario == "DEFAULT" and patient.patient_code == "PT-5044"):
                        # Moderate risk profile (Rajesh Kumar): Right heel pressure elevation & 33.6°C temp
                        temp_r = round(33.6 + sine_wave * 0.2, 1)
                        temp_l = temp_r
                        asym = round(9.5 + sine_wave * 0.8, 1)
                        base_p['R_heel'] = round(58.0 + sine_wave * 3.0 + noise, 1)
                    
                    else:  # LOW / NORMAL
                        # Low risk profile (Anita Sharma): Normal right foot temperature & baseline pressures
                        temp_r = round(32.3 + sine_wave * 0.1, 1)
                        temp_l = temp_r
                        asym = round(3.2 + sine_wave * 0.3, 1)
                        for k in base_p:
                            base_p[k] = round(base_p[k] + sine_wave * 0.5 + noise * 0.2, 1)

                    gait_data = {
                        'cadence': int(100 + sine_wave * 3),
                        'asymmetry': asym,
                        'impact_g': round(1.2 + asym * 0.02, 2)
                    }

                    # Calculate Risk & Process Alerts
                    risk_res = RiskEngine.calculate(patient, base_p, temp_l, temp_r, gait_data)
                    alerts = AlertEngine.process_telemetry(self.patient_id, base_p, temp_l, temp_r, gait_data, risk_res)

                    # Save Telemetry
                    tele = Telemetry(
                        patient_id=self.patient_id,
                        timestamp=datetime.now(timezone.utc),
                        pressure_zones=base_p,
                        temperature_left=temp_l,
                        temperature_right=temp_r,
                        gait_data=gait_data,
                        risk_score=risk_res['score'],
                        is_simulated=True
                    )
                    db.session.add(tele)
                    db.session.commit()

                    # Update Device status
                    if patient.device:
                        patient.device.status = 'ONLINE'
                        patient.device.last_seen_at = datetime.now(timezone.utc)
                        db.session.commit()

                    # Broadcast payload strictly to Room `patient_{id}`
                    payload = {
                        'patient_id': self.patient_id,
                        'timestamp': tele.timestamp.strftime('%H:%M:%S'),
                        'pressure_zones': base_p,
                        'temperature_left': temp_r,
                        'temperature_right': temp_r,
                        'temp_diff': round(abs(temp_r - float(getattr(patient, 'baseline_temp', 32.2) or 32.2)), 1),
                        'gait_data': gait_data,
                        'risk_score': risk_res['score'],
                        'risk_level': risk_res['level'],
                        'risk_takeaway': risk_res['takeaway'],
                        'risk_factors': risk_res['factors'],
                        'active_alerts_count': len(alerts)
                    }

                    room_name = f"patient_{self.patient_id}"
                    socketio.emit('telemetry_update', payload, to=room_name)

            except Exception as e:
                print(f"[Simulator Error] Patient {self.patient_id}: {e}")

    def stop(self):
        self.running = False


class SimulatorManager:
    _simulators = {}

    @classmethod
    def start_all(cls, app):
        with app.app_context():
            patients = Patient.query.all()
            for p in patients:
                if p.id not in cls._simulators or not cls._simulators[p.id].is_alive():
                    sim = PatientSimulator(app, p.id)
                    cls._simulators[p.id] = sim
                    sim.start()

    @classmethod
    def set_patient_scenario(cls, patient_id, scenario):
        if patient_id in cls._simulators:
            cls._simulators[patient_id].set_scenario(scenario)
            return True
        return False
