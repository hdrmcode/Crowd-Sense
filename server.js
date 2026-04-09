const express = require('express');
const cors = require('cors');
const fs = require('fs');
const app = express();
const PORT = 3000;

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.static('public'));

// Add cache-control headers
app.use((req, res, next) => {
  res.setHeader('Cache-Control', 'no-store, no-cache, must-revalidate, private');
  res.setHeader('Pragma', 'no-cache');
  res.setHeader('Expires', '0');
  next();
});

// Serve dashboard at root
app.get('/', (req, res) => {
  res.sendFile(__dirname + '/public/index.html');
});

// Store heart rate data in memory
let heartRateData = [];
let alerts = [];

// Endpoint to receive heart rate and SpO2 data from ESP8266
app.post('/api/heartrate', (req, res) => {
  const { user_id, location, heart_rate, spo2, signal_quality, total_beats } = req.body;
  
  console.log(`\n📊 Data received from ${user_id} at ${location}:`);
  console.log(`   Heart Rate: ${heart_rate} BPM`);
  console.log(`   SpO2: ${spo2}%`);
  console.log(`   Signal Quality: ${signal_quality}`);
  console.log(`   Total Beats: ${total_beats}`);
  
  // Create data object
  const dataPoint = {
    id: heartRateData.length + 1,
    user_id: user_id || 'USER_001',
    location: location || 'CROWD_ZONE_A',
    heart_rate: heart_rate,
    spo2: spo2 || 0,
    signal_quality: signal_quality,
    total_beats: total_beats,
    timestamp: Date.now(),
    datetime: new Date().toISOString()
  };
  
  // Store in array
  heartRateData.push(dataPoint);
  
  // Keep only last 1000 data points
  if (heartRateData.length > 1000) {
    heartRateData.shift();
  }
  
  // Check for anomalies
  let alertMessage = "";
  let severity = "";
  
  if (heart_rate > 120) {
    severity = 'HIGH';
    alertMessage = `🚨 CRITICAL: Heart rate too high (${heart_rate} BPM) - Possible stroke risk!`;
  } 
  else if (heart_rate < 50 && heart_rate > 0) {
    severity = 'HIGH';
    alertMessage = `🚨 CRITICAL: Heart rate too low (${heart_rate} BPM) - Medical attention needed!`;
  }
  else if (spo2 < 90 && spo2 > 0) {
    severity = 'HIGH';
    alertMessage = `🚨 CRITICAL: Low blood oxygen (${spo2}%) - Respiratory issue possible!`;
  }
  
  if (alertMessage) {
    const alert = {
      id: alerts.length + 1,
      user_id: user_id,
      location: location,
      heart_rate: heart_rate,
      spo2: spo2,
      severity: severity,
      message: alertMessage,
      timestamp: Date.now(),
      datetime: new Date().toISOString()
    };
    alerts.push(alert);
    console.log(`🚨 ALERT: ${alert.message}`);
  }
  
  // Save to file
  saveToFile();
  
  res.json({ 
    status: 'success', 
    message: 'Data received',
    alerts: alerts.length 
  });
});

// Endpoint to get latest data
app.get('/api/latest', (req, res) => {
  const latest = heartRateData.slice(-20);
  res.json(latest);
});

// Endpoint to get all data
app.get('/api/all', (req, res) => {
  res.json(heartRateData);
});

// Endpoint to get alerts
app.get('/api/alerts', (req, res) => {
  res.json(alerts.slice(-50));
});

// Endpoint to clear alerts
app.delete('/api/alerts', (req, res) => {
  alerts = [];
  saveToFile();
  res.json({ status: 'success', message: 'Alerts cleared' });
});

// Endpoint to get statistics
app.get('/api/stats', (req, res) => {
  if (heartRateData.length === 0) {
    res.json({ message: 'No data yet' });
    return;
  }
  
  const recentData = heartRateData.slice(-100);
  const heartRates = recentData.map(d => d.heart_rate);
  const spo2Values = recentData.map(d => d.spo2).filter(s => s > 0);
  
  const avgHR = heartRates.reduce((a, b) => a + b, 0) / heartRates.length;
  const maxHR = Math.max(...heartRates);
  const minHR = Math.min(...heartRates);
  
  const avgSpO2 = spo2Values.length > 0 ? 
    spo2Values.reduce((a, b) => a + b, 0) / spo2Values.length : 0;
  
  res.json({
    total_readings: heartRateData.length,
    average_heart_rate: avgHR.toFixed(1),
    average_spo2: avgSpO2.toFixed(1),
    max_heart_rate: maxHR,
    min_heart_rate: minHR,
    active_alerts: alerts.length,
    last_update: new Date().toISOString()
  });
});

// Save data to file
function saveToFile() {
  const data = {
    heartRateData: heartRateData,
    alerts: alerts,
    lastUpdated: new Date().toISOString()
  };
  fs.writeFileSync('data.json', JSON.stringify(data, null, 2));
}

// Load data from file
function loadFromFile() {
  try {
    if (fs.existsSync('data.json')) {
      const data = JSON.parse(fs.readFileSync('data.json'));
      heartRateData = data.heartRateData || [];
      alerts = data.alerts || [];
      console.log(`✅ Loaded ${heartRateData.length} readings and ${alerts.length} alerts`);
    }
  } catch (error) {
    console.log('No existing data file found');
  }
}

// Start server
app.listen(PORT, () => {
  console.log(`\n=================================`);
  console.log(`🚀 CrowdSense Backend Server (HR + SpO2)`);
  console.log(`=================================`);
  console.log(`✅ Server running on http://localhost:${PORT}`);
  console.log(`📊 Dashboard: http://localhost:${PORT}`);
  console.log(`📡 API Endpoint: http://10.215.21.17:${PORT}/api/heartrate`);
  console.log(`=================================\n`);
  loadFromFile();
});