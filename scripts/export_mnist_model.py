#!/usr/bin/env python3
# Script fait par IA 

"""
scripts/export_mnist_model.py
Entraîne un réseau de neurones MLP (784 -> 64 -> 10) sur MNIST avec PyTorch,
le quantifie en INT8 (symétrique / fixed-point), et exporte sw/mnist_model.h.
"""

import os
import torch
import torch.nn as nn
import torch.optim as optim
from torchvision import datasets, transforms
import numpy as np

# 1. Définition de l'architecture du réseau
class MNISTNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.fc1 = nn.Linear(784, 64, bias=True)
        self.relu = nn.ReLU()
        self.fc2 = nn.Linear(64, 10, bias=True)

    def forward(self, x):
        x = x.view(-1, 784)
        x = self.relu(self.fc1(x))
        x = self.fc2(x)
        return x

def main():
    print("=== 1. Entraînement du modèle MNIST (PyTorch) ===")
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Périphérique d'entraînement : {device}")

    transform = transforms.Compose([
        transforms.ToTensor(),
        # Normalisation [0, 1]
    ])

    data_dir = "data"
    os.makedirs(data_dir, exist_ok=True)
    train_dataset = datasets.MNIST(data_dir, train=True, download=True, transform=transform)
    test_dataset  = datasets.MNIST(data_dir, train=False, download=True, transform=transform)

    train_loader = torch.utils.data.DataLoader(train_dataset, batch_size=64, shuffle=True)
    test_loader  = torch.utils.data.DataLoader(test_dataset, batch_size=1000, shuffle=False)

    model = MNISTNet().to(device)
    optimizer = optim.Adam(model.parameters(), lr=0.003)
    criterion = nn.CrossEntropyLoss()

    # Entraînement rapide sur 2 époques (suffisant pour >95% de précision)
    model.train()
    for epoch in range(1, 3):
        total_loss = 0
        for data, target in train_loader:
            data, target = data.to(device), target.to(device)
            optimizer.zero_grad()
            output = model(data)
            loss = criterion(output, target)
            loss.backward()
            optimizer.step()
            total_loss += loss.item()
        print(f"Époque {epoch}/2 - Loss: {total_loss / len(train_loader):.4f}")

    # Évaluation de la précision en virgule flottante
    model.eval()
    correct = 0
    with torch.no_grad():
        for data, target in test_loader:
            data, target = data.to(device), target.to(device)
            pred = model(data).argmax(dim=1)
            correct += pred.eq(target).sum().item()
    acc = 100.0 * correct / len(test_dataset)
    print(f"Précision FP32 sur test : {acc:.2f}%\n")

    print("=== 2. Quantification INT8 du modèle ===")
    w1 = model.fc1.weight.detach().cpu().numpy()  # (64, 784)
    b1 = model.fc1.bias.detach().cpu().numpy()    # (64,)
    w2 = model.fc2.weight.detach().cpu().numpy()  # (10, 64)
    b2 = model.fc2.bias.detach().cpu().numpy()    # (10,)

    # Échelle d'entrée : [0.0, 1.0] -> [0, 127] (INT8 positif)
    input_scale = 127.0

    # Échelle de W1 : max(|W1|) -> 127
    scale_w1 = 127.0 / np.max(np.abs(w1))
    w1_int8 = np.clip(np.round(w1 * scale_w1), -128, 127).astype(np.int8)
    # Biais 1 : multiplié par (scale_w1 * input_scale) pour correspondre à (W1_int8 * X_int8)
    b1_int32 = np.round(b1 * scale_w1 * input_scale).astype(np.int32)

    # Facteur de décalage (Shift 1) pour requantifier la sortie de ReLU dans [0, 127]
    # On simule un passage sur 1000 images pour calibrer le shift optimal
    with torch.no_grad():
        sample_x = next(iter(test_loader))[0][:100].view(-1, 784).numpy()
    x_int = np.clip(np.round(sample_x * input_scale), 0, 127).astype(np.int8)

    # Calcul de H1_acc = (X * W1.T) + B1
    h1_acc = np.dot(x_int.astype(np.int32), w1_int8.T.astype(np.int32)) + b1_int32
    h1_relu = np.maximum(0, h1_acc)
    max_h1 = np.max(h1_relu)
    
    # Trouver le décalage binaire (shift) pour ramener max_h1 sous 127
    shift1 = max(0, int(np.ceil(np.log2(max_h1 / 127.0)))) if max_h1 > 127 else 0
    print(f"Max activation H1 : {max_h1} -> Shift 1 calculé : >> {shift1}")

    h1_quant = np.clip(h1_relu >> shift1, 0, 127).astype(np.int8)

    # Échelle de W2
    scale_w2 = 127.0 / np.max(np.abs(w2))
    w2_int8 = np.clip(np.round(w2 * scale_w2), -128, 127).astype(np.int8)
    b2_int32 = np.round(b2 * scale_w2 * 127.0).astype(np.int32)

    # Test de précision du modèle entièrement quantifié INT8 en Python
    h2_acc = np.dot(h1_quant.astype(np.int32), w2_int8.T.astype(np.int32)) + b2_int32
    preds_int8 = np.argmax(h2_acc, axis=1)
    sample_y = next(iter(test_loader))[1][:100].numpy()
    acc_int8 = 100.0 * np.sum(preds_int8 == sample_y) / len(sample_y)
    print(f"Précision modèle INT8 sur échantillon : {acc_int8:.1f}%\n")

    print("=== 3. Exportation de sw/mnist_model.h ===")
    # Sélection de 3 images de test réelles (indices 0, 1, 2 du dataset de test)
    test_imgs = []
    test_labels = []
    for idx in [0, 1, 2]:
        img, label = test_dataset[idx]
        img_np = np.clip(np.round(img.squeeze().numpy() * input_scale), 0, 127).astype(np.int8)
        test_imgs.append(img_np.flatten())
        test_labels.append(label)

    os.makedirs("sw", exist_ok=True)
    out_header = "sw/mnist_model.h"
    with open(out_header, "w") as f:
        f.write("/*\n")
        f.write(" * mnist_model.h : Poids quantifiés INT8 et images de test MNIST\n")
        f.write(" * Généré automatiquement par scripts/export_mnist_model.py\n")
        f.write(" */\n\n")
        f.write("#ifndef MNIST_MODEL_H\n")
        f.write("#define MNIST_MODEL_H\n\n")
        f.write("#include \"libaccel.h\"\n\n")
        f.write("#define MNIST_IN_DIM   784\n")
        f.write("#define MNIST_H1_DIM   64\n")
        f.write("#define MNIST_OUT_DIM  10\n")
        f.write(f"#define MNIST_SHIFT_1  {shift1}\n\n")

        # Poids W1 (64 x 784)
        f.write("/* Matrice de poids W1 (64 neurones x 784 entrées = 50 176 octets INT8) */\n")
        f.write("static const int8_t mnist_w1[64 * 784] = {\n")
        for i, val in enumerate(w1_int8.flatten()):
            f.write(f"{val:4d},")
            if (i + 1) % 16 == 0: f.write("\n")
        f.write("};\n\n")

        # Biais B1 (64 x int32)
        f.write("/* Biais B1 (64 entiers 32-bit) */\n")
        f.write("static const int32_t mnist_b1[64] = {\n")
        for i, val in enumerate(b1_int32):
            f.write(f"{val:8d},")
            if (i + 1) % 8 == 0: f.write("\n")
        f.write("};\n\n")

        # Poids W2 (10 x 64)
        f.write("/* Matrice de poids W2 (10 neurones x 64 entrées = 640 octets INT8) */\n")
        f.write("static const int8_t mnist_w2[10 * 64] = {\n")
        for i, val in enumerate(w2_int8.flatten()):
            f.write(f"{val:4d},")
            if (i + 1) % 16 == 0: f.write("\n")
        f.write("};\n\n")

        # Biais B2 (10 x int32)
        f.write("/* Biais B2 (10 entiers 32-bit) */\n")
        f.write("static const int32_t mnist_b2[10] = {\n")
        for i, val in enumerate(b2_int32):
            f.write(f"{val:8d},")
        f.write("\n};\n\n")

        # Images de test
        f.write("/* 3 Images de test réelles issues de MNIST (28x28 = 784 octets) */\n")
        f.write(f"static const int mnist_test_labels[3] = {{{test_labels[0]}, {test_labels[1]}, {test_labels[2]}}};\n\n")
        for idx in range(3):
            f.write(f"/* Image de test {idx} - Chiffre attendu : {test_labels[idx]} */\n")
            f.write(f"static const int8_t mnist_test_img_{idx}[784] = {{\n")
            for i, val in enumerate(test_imgs[idx]):
                f.write(f"{val:4d},")
                if (i + 1) % 16 == 0: f.write("\n")
            f.write("};\n\n")

        f.write("#endif /* MNIST_MODEL_H */\n")

    print(f"Modèle exporté avec succès dans {out_header} (Taille : {os.path.getsize(out_header)/1024:.1f} Ko) !")

if __name__ == "__main__":
    main()