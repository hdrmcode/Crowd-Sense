import pandas as pd
import joblib

# ✅ Load the trained model
model = joblib.load("xgb_heart_model.pkl")
print("✅ Model loaded successfully!")

# ✅ Example: Load the same dataset (to simulate real inputs)
data = pd.read_excel("sensor_data.xlsx")

# Recreate feature extraction (same as training)
window_size = 5
features = []

for i in range(0, len(data) - window_size):
    window = data.iloc[i:i+window_size]
    mean_hr = window['heart_rate'].mean()
    std_hr = window['heart_rate'].std()
    mean_spo2 = window['spo2'].mean()
    hr_slope = window['heart_rate'].iloc[-1] - window['heart_rate'].iloc[0]
    spo2_slope = window['spo2'].iloc[-1] - window['spo2'].iloc[0]
    label = window['label'].mode()[0]
    
    features.append([mean_hr, std_hr, mean_spo2, hr_slope, spo2_slope, label])

df = pd.DataFrame(features, columns=["mean_hr","std_hr","mean_spo2","hr_slope","spo2_slope","label"])

# Split into features/labels
X = df.drop("label", axis=1)
y_true = df["label"]

# ✅ Predict on the data
y_pred = model.predict(X)

# ✅ Evaluate performance
from sklearn.metrics import classification_report, accuracy_score

print("\n📊 Test Accuracy:", accuracy_score(y_true, y_pred))
print(classification_report(y_true, y_pred))

# ✅ Example: Predict a few new inputs
sample = pd.DataFrame({
    "mean_hr": [80, 130, 55],
    "std_hr": [3, 10, 4],
    "mean_spo2": [98, 90, 93],
    "hr_slope": [1, 5, -3],
    "spo2_slope": [0, -2, -1]
})

preds = model.predict(sample)
print("\n🧠 Predictions for new samples:", preds)
