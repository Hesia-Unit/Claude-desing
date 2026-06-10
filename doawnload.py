from datasets import load_dataset

dataset = load_dataset("OpenAssistant/oasst1")

dataset.save_to_disk(r"F:\Set-Donner\datasets\conversation\oasst1")