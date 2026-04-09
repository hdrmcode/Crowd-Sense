import pandas as pd
import numpy as np
from xgboost import XGBClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, confusion_matrix, accuracy_score
import joblib

# ✅ Load sensor data (Excel file)
# Columns: time, heart_rate, spo2, label (0=normal, 1=abnormal)
data = pd.read_excel("sensor_data.xlsx")

# ✅ Feature extraction (5-sec window summary)
window_size = 5  # seconds
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

# ✅ Split data
X = df.drop("label", axis=1)
y = df["label"]
X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)

# ✅ XGBoost model
model = XGBClassifier(
    n_estimators=100,
    max_depth=4,
    learning_rate=0.1,
    subsample=0.8,
    colsample_bytree=0.8,
    tree_method="hist",  # fast histogram-based training
    use_label_encoder=False,
    eval_metric="logloss"
)

# ✅ Train model
print("🚀 Training model...")
model.fit(X_train, y_train)

# ✅ Evaluate
y_pred = model.predict(X_test)
print("\n📊 Accuracy:", accuracy_score(y_test, y_pred))
print(classification_report(y_test, y_pred))

# ✅ Save model for deployment
joblib.dump(model, "xgb_heart_model.pkl")
print("\n💾 Model saved as xgb_heart_model.pkl")
