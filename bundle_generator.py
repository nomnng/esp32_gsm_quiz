import struct

questions = []

def create_question(mp3_path, yes_points, no_points):
	mp3_file = open(mp3_path, "rb")
	mp3_data = mp3_file.read()
	mp3_file.close()

	yes_points_list = list(yes_points.values())
	no_points_list = list(no_points.values())
	header = struct.pack("<I7b7b", len(mp3_data), *yes_points_list, *no_points_list)

	questions.append(header + mp3_data)


def create_result_audio(mp3_path):
	mp3_file = open(mp3_path, "rb")
	mp3_data = mp3_file.read()
	mp3_file.close()

	header = struct.pack("<I7b7b", len(mp3_data), *([0] * 14))

	return header + mp3_data


f = open("build/bundle.bin", "wb")

data = b"" 

create_question(
	"mp3/Q1.mp3",
	{"Result1":1, "Result2":2, "Result3":3, "Result4":0, "Result5":0, "Result6":0, "Result7":0},
	{"Result1":0, "Result2":0, "Result3":0, "Result4":-3, "Result5":-2, "Result6":-1, "Result7":0},
)

create_question(
	"mp3/Q2.mp3",
	{"Result1":1, "Result2":2, "Result3":3, "Result4":0, "Result5":0, "Result6":0, "Result7":0},
	{"Result1":0, "Result2":0, "Result3":0, "Result4":-3, "Result5":-2, "Result6":-1, "Result7":0},
)

create_question(
	"mp3/Q3.mp3",
	{"Result1":1, "Result2":2, "Result3":3, "Result4":0, "Result5":0, "Result6":0, "Result7":0},
	{"Result1":0, "Result2":0, "Result3":0, "Result4":-3, "Result5":-2, "Result6":-1, "Result7":0},
)

data += struct.pack("<I", len(questions))
for i in questions:
	data += i

data += create_result_audio("mp3/result_1.mp3")
data += create_result_audio("mp3/result_2.mp3")
data += create_result_audio("mp3/result_3.mp3")
data += create_result_audio("mp3/result_4.mp3")
data += create_result_audio("mp3/result_5.mp3")
data += create_result_audio("mp3/result_6.mp3")
data += create_result_audio("mp3/result_7.mp3")

print(len(data))

f.write(data)
f.close()
