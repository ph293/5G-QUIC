import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
OUTPUT_HTML = os.path.join(PROJECT_ROOT, "analysis_plots", "figures", "interactive_sdsm_topology.html")
os.makedirs(os.path.dirname(OUTPUT_HTML), exist_ok=True)

html_content = """<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Multi-Lane 5G Smart Corridor - QUIC SDSM Simulation</title>
    <style>
        * { box-sizing: border-box; }
        body {
            margin: 0;
            background: linear-gradient(180deg, #dbeafe 0%, #bfdbfe 40%, #cbd5e1 100%);
            color: #1e293b;
            font-family: 'Segoe UI', Arial, sans-serif;
            overflow: hidden;
        }

        #topDashboard {
            width: 100%;
            background: rgba(255, 255, 255, 0.96);
            border-bottom: 2px solid #94a3b8;
            box-shadow: 0 4px 16px rgba(0,0,0,0.08);
            padding: 8px 24px;
            display: flex;
            align-items: center;
            justify-content: space-between;
            flex-wrap: wrap;
            gap: 12px;
            position: absolute;
            top: 0; left: 0; z-index: 20;
        }

        .controls-group { display: flex; align-items: center; gap: 12px; }
        button {
            background: #2563eb; color: #fff; border: none; padding: 6px 14px;
            font-weight: 600; font-size: 13px; border-radius: 6px; cursor: pointer; transition: 0.2s;
        }
        button:hover { background: #1d4ed8; }

        .telemetry-group { display: flex; align-items: center; gap: 16px; font-size: 13px; }
        .telemetry-item { display: flex; align-items: center; gap: 6px; }
        .badge { display: inline-block; padding: 2px 8px; border-radius: 5px; font-weight: 700; color: #fff; font-size: 12px; }

        canvas { display: block; margin-top: 50px; }
    </style>
</head>
<body>

    <div id="topDashboard">
        <div class="controls-group">
            <span style="font-weight: bold; color: #1e40af; font-size: 14px;">🛣️ Multi-Lane 5G MEC Highway</span>
            <button id="playBtn" onclick="togglePlay()">Pause</button>
            <button onclick="resetSim()">Restart</button>
            <span style="font-weight: 600;">Time: <span id="timeDisplay" style="color: #2563eb;">0.0s</span></span>
            <input type="range" id="timeSlider" min="0" max="25" step="0.1" value="0" oninput="seekTime(this.value)" style="cursor: pointer; width: 140px;">
        </div>

        <div class="telemetry-group">
            <div class="telemetry-item">
                <span>Active Cell:</span>
                <span id="towerStatus" class="badge" style="background:#2563eb;">Tower 1</span>
            </div>
            <div class="telemetry-item">
                <span>Edge Server:</span>
                <span id="mecStatus" class="badge" style="background:#16a34a;">MEC Server 1</span>
            </div>
            <div class="telemetry-item">
                <span>SDN Ctrl 0:</span>
                <span id="c0Status" class="badge" style="background:#dc2626;">Overloaded (92.4)</span>
            </div>
            <div class="telemetry-item">
                <span>SDN Ctrl 1:</span>
                <span id="c1Status" class="badge" style="background:#64748b;">Standby</span>
            </div>
            <div class="telemetry-item">
                <span>QUIC CID:</span>
                <code style="color: #ea580c; font-weight: bold;">0xabcdef12345678</code>
            </div>
        </div>
    </div>

    <canvas id="simCanvas"></canvas>

    <script>
        const canvas = document.getElementById('simCanvas');
        const ctx = canvas.getContext('2d');
        function resize() {
            canvas.width = window.innerWidth;
            canvas.height = window.innerHeight - 50;
        }
        window.addEventListener('resize', resize);
        resize();

        let simTime = 0.0;
        let isPlaying = true;
        let lastTimestamp = performance.now();
        const numTowers = 5;

        // Background traffic vehicles definition
        const bgVehicles = [
            { speedFactor: 0.85, lane: 0, color: "#f59e0b", width: 44, height: 18, type: "van", isConnected: true },
            { speedFactor: 1.15, lane: 1, color: "#d97706", width: 38, height: 16, type: "car", isConnected: true },
            { speedFactor: -0.9, lane: 2, color: "#dc2626", width: 40, height: 17, type: "car", isConnected: false },
            { speedFactor: -0.6, lane: 3, color: "#475569", width: 65, height: 22, type: "truck", isConnected: false },
            { speedFactor: -1.05,lane: 2, color: "#059669", width: 42, height: 17, type: "car", isConnected: false }
        ];

        function render(now) {
            const dt = (now - lastTimestamp) / 1000.0;
            lastTimestamp = now;

            if (isPlaying) {
                simTime += dt;
                if (simTime > 25.0) simTime = 0.0;
                document.getElementById('timeSlider').value = simTime;
            }

            document.getElementById('timeDisplay').innerText = simTime.toFixed(1) + 's';
            ctx.clearRect(0, 0, canvas.width, canvas.height);

            const cy = canvas.height / 2;
            const startX = 140;
            const endX = canvas.width - 140;
            const spacing = (endX - startX) / (numTowers - 1);
            let activeIdx = Math.min(4, Math.floor(simTime / 5.0));

            // Status Update
            let c0Bg = "#dc2626", c0Txt = "Overloaded (92.4)";
            let c1Bg = "#64748b", c1Txt = "Standby";

            if (activeIdx >= 1) {
                c0Bg = "#16a34a"; c0Txt = "Rebalanced (38.5)";
                c1Bg = "#16a34a"; c1Txt = "SNMO Active (40.2)";
            }

            document.getElementById('towerStatus').innerText = `Tower ${activeIdx + 1} (gNodeB ${activeIdx + 1})`;
            document.getElementById('mecStatus').innerText = `MEC Server ${activeIdx + 1}`;
            document.getElementById('c0Status').innerText = c0Txt;
            document.getElementById('c0Status').style.background = c0Bg;
            document.getElementById('c1Status').innerText = c1Txt;
            document.getElementById('c1Status').style.background = c1Bg;

            // 1. DRAW DUAL-CARRIAGEWAY MULTI-LANE HIGHWAY
            const roadTop = cy - 40;
            const laneH = 34;
            const roadTotalH = laneH * 4 + 14; // 4 lanes + center median

            // Outer asphalt
            ctx.fillStyle = "#334155";
            ctx.fillRect(0, roadTop, canvas.width, roadTotalH);

            // Road shoulders
            ctx.fillStyle = "#cbd5e1";
            ctx.fillRect(0, roadTop - 4, canvas.width, 4);
            ctx.fillRect(0, roadTop + roadTotalH, canvas.width, 4);

            // Center Concrete Median Divider
            const medianY = roadTop + laneH * 2;
            ctx.fillStyle = "#64748b";
            ctx.fillRect(0, medianY, canvas.width, 14);
            ctx.fillStyle = "#94a3b8";
            ctx.fillRect(0, medianY + 4, canvas.width, 6);

            // White dashed lane markings (Lane 1 & 2 eastbound, Lane 3 & 4 westbound)
            ctx.strokeStyle = "#f8fafc";
            ctx.lineWidth = 3;
            ctx.setLineDash([25, 20]);

            ctx.beginPath();
            ctx.moveTo(0, roadTop + laneH);
            ctx.lineTo(canvas.width, roadTop + laneH);
            ctx.moveTo(0, medianY + 14 + laneH);
            ctx.lineTo(canvas.width, medianY + 14 + laneH);
            ctx.stroke();
            ctx.setLineDash([]);

            // 2. DRAW SDN CONTROLLER CLUSTER AT TOP
            const ctrlY = cy - 215;
            const ctrlSpacing = 280;
            const ctrlX0 = canvas.width / 2 - ctrlSpacing;
            const ctrlX1 = canvas.width / 2;
            const ctrlX2 = canvas.width / 2 + ctrlSpacing;

            function drawControllerBox(x, y, name, status, color) {
                ctx.fillStyle = "#ffffff";
                ctx.strokeStyle = color;
                ctx.lineWidth = 3;
                ctx.beginPath();
                ctx.roundRect(x - 75, y - 20, 150, 42, 8);
                ctx.fill();
                ctx.stroke();

                ctx.fillStyle = "#0f172a";
                ctx.font = "bold 12px sans-serif";
                ctx.textAlign = "center";
                ctx.fillText(name, x, y - 2);

                ctx.fillStyle = color;
                ctx.font = "bold 11px sans-serif";
                ctx.fillText(status, x, y + 13);
            }

            ctx.strokeStyle = "#f97316";
            ctx.lineWidth = 3;
            ctx.setLineDash([6, 6]);
            ctx.beginPath();
            ctx.moveTo(ctrlX0 + 75, ctrlY);
            ctx.lineTo(ctrlX1 - 75, ctrlY);
            ctx.moveTo(ctrlX1 + 75, ctrlY);
            ctx.lineTo(ctrlX2 - 75, ctrlY);
            ctx.stroke();
            ctx.setLineDash([]);

            ctx.fillStyle = "#ea580c";
            ctx.font = "bold 11px sans-serif";
            ctx.textAlign = "center";
            ctx.fillText("SNMO Channel", (ctrlX0 + ctrlX1) / 2, ctrlY - 7);
            ctx.fillText("SNMO Channel", (ctrlX1 + ctrlX2) / 2, ctrlY - 7);

            drawControllerBox(ctrlX0, ctrlY, "SDN Controller 0", c0Txt, c0Bg);
            drawControllerBox(ctrlX1, ctrlY, "SDN Controller 1", c1Txt, c1Bg);
            drawControllerBox(ctrlX2, ctrlY, "SDN Controller 2", "Standby", "#64748b");

            // 3. DRAW 5G TOWERS & MEC EDGE SERVERS
            for (let i = 0; i < numTowers; i++) {
                const tx = startX + i * spacing;
                const ty = cy - 100;
                const my = cy + 185;
                const isActive = (i === activeIdx);

                // Control Plane Link
                let parentCtrlX = (i <= 1) ? ctrlX0 : ((i <= 3) ? ctrlX1 : ctrlX2);
                ctx.strokeStyle = isActive ? "#16a34a" : "#cbd5e1";
                ctx.lineWidth = isActive ? 2.5 : 1;
                ctx.setLineDash(isActive ? [5, 3] : [2, 2]);
                ctx.beginPath();
                ctx.moveTo(parentCtrlX, ctrlY + 22);
                ctx.lineTo(tx, ty - 45);
                ctx.stroke();
                ctx.setLineDash([]);

                // Fiber Cable to MEC Server
                ctx.strokeStyle = "#94a3b8";
                ctx.lineWidth = 3;
                ctx.beginPath();
                ctx.moveTo(tx, ty + 10);
                ctx.lineTo(tx, my - 25);
                ctx.stroke();

                // Inter-MEC Orange Optical Cable
                if (i < numTowers - 1) {
                    ctx.strokeStyle = "#f97316";
                    ctx.lineWidth = 5;
                    ctx.beginPath();
                    ctx.moveTo(tx + 45, my);
                    ctx.lineTo(tx + spacing - 45, my);
                    ctx.stroke();
                }

                // DRAW LATTICE TOWER
                ctx.strokeStyle = isActive ? "#0284c7" : "#64748b";
                ctx.lineWidth = 2.5;

                ctx.beginPath();
                ctx.moveTo(tx - 20, ty);
                ctx.lineTo(tx, ty - 50);
                ctx.lineTo(tx + 20, ty);
                ctx.stroke();

                ctx.beginPath();
                ctx.moveTo(tx - 14, ty - 18); ctx.lineTo(tx + 14, ty - 18);
                ctx.moveTo(tx - 8, ty - 34);  ctx.lineTo(tx + 8, ty - 34);
                ctx.moveTo(tx - 14, ty - 18); ctx.lineTo(tx + 8, ty - 34);
                ctx.moveTo(tx + 14, ty - 18); ctx.lineTo(tx - 8, ty - 34);
                ctx.stroke();

                ctx.lineWidth = 3;
                ctx.beginPath();
                ctx.moveTo(tx, ty - 50); ctx.lineTo(tx, ty - 65);
                ctx.stroke();

                ctx.fillStyle = isActive ? "#ef4444" : "#94a3b8";
                ctx.beginPath();
                ctx.arc(tx, ty - 66, 4, 0, Math.PI * 2);
                ctx.fill();

                if (isActive) {
                    ctx.strokeStyle = "rgba(2, 132, 199, 0.4)";
                    ctx.lineWidth = 2;
                    ctx.beginPath();
                    ctx.arc(tx, ty - 50, 25, -Math.PI * 0.75, -Math.PI * 0.25);
                    ctx.arc(tx, ty - 50, 42, -Math.PI * 0.75, -Math.PI * 0.25);
                    ctx.stroke();
                }

                ctx.fillStyle = "#0f172a";
                ctx.font = "bold 12px sans-serif";
                ctx.fillText(`Tower ${i + 1}`, tx, ty + 18);

                // DRAW MEC SERVER RACK
                ctx.fillStyle = isActive ? "#dcfce7" : "#ffffff";
                ctx.strokeStyle = isActive ? "#16a34a" : "#94a3b8";
                ctx.lineWidth = isActive ? 3 : 2;
                ctx.beginPath();
                ctx.roundRect(tx - 45, my - 24, 90, 48, 6);
                ctx.fill();
                ctx.stroke();

                ctx.strokeStyle = "#cbd5e1";
                ctx.lineWidth = 1;
                for (let s = -10; s <= 15; s += 12) {
                    ctx.beginPath();
                    ctx.moveTo(tx - 38, my + s);
                    ctx.lineTo(tx + 38, my + s);
                    ctx.stroke();
                }

                ctx.fillStyle = isActive ? "#22c55e" : "#cbd5e1";
                ctx.beginPath();
                ctx.arc(tx + 30, my - 13, 3, 0, Math.PI * 2);
                ctx.arc(tx + 30, my + 1, 3, 0, Math.PI * 2);
                ctx.fill();

                ctx.fillStyle = "#0f172a";
                ctx.font = "bold 12px sans-serif";
                ctx.fillText(`MEC ${i + 1}`, tx, my + 38);
            }

            // 4. DRAW BACKGROUND HIGHWAY TRAFFIC
            bgVehicles.forEach((v) => {
                let vX;
                if (v.speedFactor > 0) {
                    vX = ((simTime * 90 * v.speedFactor) % (canvas.width + 120)) - 60;
                } else {
                    vX = canvas.width + 60 - ((simTime * 90 * Math.abs(v.speedFactor)) % (canvas.width + 120));
                }

                let laneY = roadTop + (v.lane * laneH) + laneH / 2;
                if (v.lane >= 2) laneY += 14; // adjust for median

                // Vehicle body
                ctx.fillStyle = v.color;
                ctx.beginPath();
                ctx.roundRect(vX - v.width/2, laneY - v.height/2, v.width, v.height, 4);
                ctx.fill();

                // Wheels
                ctx.fillStyle = "#0f172a";
                ctx.fillRect(vX - v.width/2 + 5, laneY - v.height/2 - 2, 7, 3);
                ctx.fillRect(vX + v.width/2 - 12, laneY - v.height/2 - 2, 7, 3);
                ctx.fillRect(vX - v.width/2 + 5, laneY + v.height/2 - 1, 7, 3);
                ctx.fillRect(vX + v.width/2 - 12, laneY + v.height/2 - 1, 7, 3);

                // Small telemetry link for connected fleet vehicles
                if (v.isConnected) {
                    const nearIdx = Math.min(4, Math.max(0, Math.floor((vX - startX) / spacing)));
                    const nearTx = startX + nearIdx * spacing;
                    ctx.strokeStyle = "rgba(245, 158, 11, 0.3)";
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    ctx.moveTo(vX, laneY - 10);
                    ctx.lineTo(nearTx, cy - 100);
                    ctx.stroke();
                }
            });

            // 5. DRAW PRIMARY TEST VEHICLE (CAR 1 - BLUE) WITH QUIC SDSM BEAM
            const carProgress = simTime / 25.0;
            const carX = startX + (endX - startX) * carProgress;
            const carY = roadTop + laneH / 2; // Eastbound Lane 0

            const activeTx = startX + activeIdx * spacing;

            // QUIC Wireless Migration Beam
            ctx.strokeStyle = "rgba(37, 99, 235, 0.75)";
            ctx.lineWidth = 3;
            ctx.beginPath();
            ctx.moveTo(carX, carY - 12);
            ctx.lineTo(activeTx, cy - 100);
            ctx.stroke();

            // Real-time Moving QUIC Data Pulse
            const pulseT = (simTime * 4.0) % 1.0;
            const px = carX + (activeTx - carX) * pulseT;
            const py = (carY - 12) + ((cy - 100) - (carY - 12)) * pulseT;
            ctx.fillStyle = "#22c55e";
            ctx.beginPath();
            ctx.arc(px, py, 6, 0, Math.PI * 2);
            ctx.fill();

            // Primary Car Body
            ctx.fillStyle = "#2563eb";
            ctx.beginPath();
            ctx.roundRect(carX - 25, carY - 9, 50, 18, 4);
            ctx.fill();

            // Car windshield
            ctx.fillStyle = "#bfdbfe";
            ctx.beginPath();
            ctx.roundRect(carX - 10, carY - 7, 24, 14, 2);
            ctx.fill();

            // Headlights & Taillights
            ctx.fillStyle = "#fde047";
            ctx.fillRect(carX + 23, carY - 7, 3, 4);
            ctx.fillRect(carX + 23, carY + 3, 3, 4);
            ctx.fillStyle = "#ef4444";
            ctx.fillRect(carX - 25, carY - 7, 3, 4);
            ctx.fillRect(carX - 25, carY + 3, 3, 4);

            // Primary Car Badge & CID Label
            ctx.fillStyle = "#1e3a8a";
            ctx.font = "bold 12px sans-serif";
            ctx.fillText("Primary 5G Test Vehicle (CID: 0xabcdef)", carX, carY - 20);

            requestAnimationFrame(render);
        }

        function togglePlay() {
            isPlaying = !isPlaying;
            document.getElementById('playBtn').innerText = isPlaying ? "Pause" : "Play";
        }

        function resetSim() {
            simTime = 0.0;
            document.getElementById('timeSlider').value = 0;
            if (!isPlaying) togglePlay();
        }

        function seekTime(val) { simTime = parseFloat(val); }

        requestAnimationFrame(render);
    </script>
</body>
</html>
"""

with open(OUTPUT_HTML, "w", encoding="utf-8") as f:
    f.write(html_content)

print(f"\n[Success] Multi-Lane Smart Highway Visualizer generated at: {OUTPUT_HTML}")
