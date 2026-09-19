import json
from datetime import datetime, timedelta, timezone
from extensions import db
from models import User, Doctor, Patient, Device, Telemetry, Alert
from risk_engine import RiskEngine
from alert_engine import AlertEngine

def seed_database():
    """
    Seeds initial doctor, 3 distinct demo patients, devices, and historical telemetry data.
    """
    # Clear existing tables for clean seed
    db.drop_all()
    db.create_all()

    print("Seeding database with demo users...")

    # 1. Seed Doctor User & Profile
    doc_user = User(
        email="doctor@smartfoot.med",
        name="Dr. Aris Thorne",
        role="DOCTOR",
        profile_photo_url="https://images.unsplash.com/photo-1622253692010-333f2da6031d?w=150&auto=format&fit=crop&q=80"
    )
    db.session.add(doc_user)
    db.session.flush()

    doctor = Doctor(
        user_id=doc_user.id,
        credentials="MD, DPM, FAFAS",
        specialty="Diabetic Limb Salvage & Podiatric Surgery"
    )
    db.session.add(doctor)
    db.session.flush()

    # 2. Patient 1: Sita Devi (High Risk)
    p1_user = User(
        email="sita@smartfoot.med",
        name="Sita Devi",
        role="PATIENT",
        profile_photo_url="https://images.unsplash.com/photo-1544005313-94ddf0286df2?w=150&auto=format&fit=crop&q=80"
    )
    db.session.add(p1_user)
    db.session.flush()

    sita_baseline_p = {
        'R_toe': 0.0, 'R_met': 0.0, 'R_arch': 0.0, 'R_heel': 0.0
    }
    patient_sita = Patient(
        user_id=p1_user.id,
        patient_code="PT-8021",
        age=62,
        gender="Female",
        assigned_doctor_id=doctor.id,
        baseline_pressure=sita_baseline_p,
        baseline_temp=32.2,
        baseline_gait=92.0,
        amputated_foot=None
    )
    db.session.add(patient_sita)
    db.session.flush()

    device_sita = Device(
        device_id="ESP32-SITA-01",
        device_token="dev-token-sita-101",
        patient_id=patient_sita.id,
        status="ONLINE",
        last_seen_at=datetime.now(timezone.utc)
    )
    db.session.add(device_sita)

    # 3. Patient 2: Rajesh Kumar (Moderate Risk)
    p2_user = User(
        email="rajesh@smartfoot.med",
        name="Rajesh Kumar",
        role="PATIENT",
        profile_photo_url="https://images.unsplash.com/photo-1507003211169-0a1dd7228f2d?w=150&auto=format&fit=crop&q=80"
    )
    db.session.add(p2_user)
    db.session.flush()

    rajesh_baseline_p = {
        'R_toe': 27.0, 'R_met': 32.0, 'R_arch': 22.0, 'R_heel': 40.0
    }
    patient_rajesh = Patient(
        user_id=p2_user.id,
        patient_code="PT-5044",
        age=58,
        gender="Male",
        assigned_doctor_id=doctor.id,
        baseline_pressure=rajesh_baseline_p,
        baseline_temp=32.8,
        baseline_gait=94.0
    )
    db.session.add(patient_rajesh)
    db.session.flush()

    device_rajesh = Device(
        device_id="ESP32-RAJESH-02",
        device_token="dev-token-rajesh-102",
        patient_id=patient_rajesh.id,
        status="ONLINE",
        last_seen_at=datetime.now(timezone.utc)
    )
    db.session.add(device_rajesh)

    # 4. Patient 3: Anita Sharma (Low Risk)
    p3_user = User(
        email="anita@smartfoot.med",
        name="Anita Sharma",
        role="PATIENT",
        profile_photo_url="https://images.unsplash.com/photo-1573496359142-b8d87734a5a2?w=150&auto=format&fit=crop&q=80"
    )
    db.session.add(p3_user)
    db.session.flush()

    anita_baseline_p = {
        'R_toe': 23.0, 'R_met': 27.0, 'R_arch': 17.0, 'R_heel': 33.0
    }
    patient_anita = Patient(
        user_id=p3_user.id,
        patient_code="PT-1109",
        age=49,
        gender="Female",
        assigned_doctor_id=doctor.id,
        baseline_pressure=anita_baseline_p,
        baseline_temp=32.0,
        baseline_gait=98.0
    )
    db.session.add(patient_anita)
    db.session.flush()

    device_anita = Device(
        device_id="ESP32-ANITA-03",
        device_token="dev-token-anita-103",
        patient_id=patient_anita.id,
        status="ONLINE",
        last_seen_at=datetime.now(timezone.utc)
    )
    db.session.add(device_anita)

    db.session.commit()

    # 5. Generate Historical Telemetry Records (Past 7 Days, 8-Zone Array)
    print("Generating distinct historical telemetry series...")
    now = datetime.now(timezone.utc)

    patients_configs = [
        (patient_sita, 82.5, 31.8, 34.4, 18.5, "Sita High Risk"),      # Sita: High temp diff ~2.6°C, high R_met pressure (~82 kPa)
        (patient_rajesh, 48.0, 32.4, 33.6, 9.5, "Rajesh Med Risk"),    # Rajesh: Med temp diff ~1.2°C, moderate R_heel pressure (~58 kPa)
        (patient_anita, 16.5, 32.1, 32.4, 3.2, "Anita Low Risk")        # Anita: Minimal temp diff ~0.3°C, normal pressures (~30-36 kPa)
    ]

    for patient, target_risk, base_tl, base_tr, base_asym, label in patients_configs:
        for i in range(14, -1, -1):
            ts = now - timedelta(hours=i * 12)
            fluct = (i % 5 - 2) * 0.4
            tl = round(base_tl + fluct * 0.2, 1)
            tr = round(base_tr + fluct * 0.3, 1)
            asym = round(max(1.0, base_asym + fluct * 0.5), 1)

            p_map = dict(patient.baseline_pressure)
            if "Sita" in label:
                p_map['R_met'] = 0.0
                p_map['R_toe'] = 0.0
                p_map['R_arch'] = 0.0
                p_map['R_heel'] = 0.0
                tl, tr = 32.2, 32.2
                asym = 0.0
            elif "Rajesh" in label:
                p_map['R_heel'] = round(54.0 + (i % 4) * 1.2, 1)
            else:
                for k in p_map:
                    p_map[k] = round(p_map[k] + (i % 3 - 1) * 0.8, 1)

            g_data = {'cadence': 102 + (i % 4), 'asymmetry': asym, 'impact_g': round(1.2 + asym * 0.02, 2)}
            risk_res = RiskEngine.calculate(patient, p_map, tl, tr, g_data)

            tele = Telemetry(
                patient_id=patient.id,
                timestamp=ts,
                pressure_zones=p_map,
                temperature_left=tl,
                temperature_right=tr,
                blood_glucose=114.0 if "Anita" in label else (142.0 if "Rajesh" in label else 168.0),
                daily_steps=4820 + (i * 150),
                gait_data=g_data,
                risk_score=risk_res['score'],
                is_simulated=True
            )
            db.session.add(tele)

            if i == 0:
                AlertEngine.process_telemetry(patient.id, p_map, tl, tr, g_data, risk_res)

    db.session.commit()
    print("Database seeding completed successfully!")


if __name__ == '__main__':
    from app import create_app
    app = create_app(start_simulators=False)
    with app.app_context():
        seed_database()
