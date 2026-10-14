--
-- PostgreSQL database dump
--

\restrict mqhfyzT1iUKkHRwfNpPww4w7brcS680LTU6Asd3zyCt7M93Pf7QuwDkHfQduuow

-- Dumped from database version 18.6
-- Dumped by pg_dump version 18.6

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET transaction_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: appointments; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.appointments (
    appointment_id integer NOT NULL,
    patient_id integer,
    doctor_id integer
);


--
-- Name: appointments_appointment_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.appointments_appointment_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: appointments_appointment_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.appointments_appointment_id_seq OWNED BY public.appointments.appointment_id;


--
-- Name: doctors; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.doctors (
    doctor_id integer NOT NULL,
    name character varying(100) NOT NULL,
    specialization character varying(100)
);


--
-- Name: doctors_import; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.doctors_import (
    doctor_id integer,
    name character varying(100),
    specialization character varying(100)
);


--
-- Name: emergency; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.emergency (
    emergency_id integer NOT NULL,
    patient_id integer,
    priority integer
);


--
-- Name: emergency_cases; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.emergency_cases (
    emergency_id integer NOT NULL,
    patient_id integer,
    priority integer,
    CONSTRAINT emergency_cases_priority_check CHECK (((priority >= 1) AND (priority <= 5)))
);


--
-- Name: emergency_cases_emergency_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.emergency_cases_emergency_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: emergency_cases_emergency_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.emergency_cases_emergency_id_seq OWNED BY public.emergency_cases.emergency_id;


--
-- Name: emergency_emergency_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.emergency_emergency_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: emergency_emergency_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.emergency_emergency_id_seq OWNED BY public.emergency.emergency_id;


--
-- Name: medical_records; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.medical_records (
    record_id integer NOT NULL,
    patient_id integer,
    disease character varying(200),
    treatment character varying(200),
    medicine character varying(200)
);


--
-- Name: medical_records_record_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.medical_records_record_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: medical_records_record_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.medical_records_record_id_seq OWNED BY public.medical_records.record_id;


--
-- Name: patients; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.patients (
    patient_id integer NOT NULL,
    name character varying(100) NOT NULL,
    age integer,
    gender character(1)
);


--
-- Name: appointments appointment_id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.appointments ALTER COLUMN appointment_id SET DEFAULT nextval('public.appointments_appointment_id_seq'::regclass);


--
-- Name: emergency emergency_id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.emergency ALTER COLUMN emergency_id SET DEFAULT nextval('public.emergency_emergency_id_seq'::regclass);


--
-- Name: emergency_cases emergency_id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.emergency_cases ALTER COLUMN emergency_id SET DEFAULT nextval('public.emergency_cases_emergency_id_seq'::regclass);


--
-- Name: medical_records record_id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.medical_records ALTER COLUMN record_id SET DEFAULT nextval('public.medical_records_record_id_seq'::regclass);


--
-- Data for Name: appointments; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.appointments (appointment_id, patient_id, doctor_id) FROM stdin;
\.


--
-- Data for Name: doctors; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.doctors (doctor_id, name, specialization) FROM stdin;
201	Dr. Vikram Chawla	Cardiology
202	Dr. Neelam Saxena	Neurology
203	Dr. Karan Oberoi	Orthopedics
204	Dr. Shalini Arora	Dermatology
205	Dr. Abhishek Tandon	Pediatrics
206	Dr. Pankaj Bansal	Gynecology
207	Dr. Ritu Mahajan	Ophthalmology
208	Dr. Mohit Suri	Psychiatry
209	Dr. Deepika Menon	ENT
210	Dr. Tarun Bhardwaj	General Medicine
211	Dr. Nitin Ahuja	Emergency Medicine
212	Dr. Swati Ghosh	Oncology
213	Dr. Akash Dutta	Nephrology
214	Dr. Monica Bedi	Gastroenterology
215	Dr. Chirag Anand	Pulmonology
216	Dr. Pallavi Desai	Endocrinology
217	Dr. Saurabh Saxena	Urology
218	Dr. Komal Sood	Radiology
219	Dr. Abhinav Kohli	Anesthesiology
220	Dr. Bhavna Arora	Pathology
221	Dr. Rajeev Puri	Infectious Diseases
222	Dr. Shweta Kulkarni	Rheumatology
223	Dr. Amitabh Sen	Plastic Surgery
224	Dr. Garima Sethi	General Surgery
225	Dr. Keshav Prasad	Family Medicine
226	Dr. Namrata Pillai	Cardiology
227	Dr. Lokesh Yadav	Neurology
228	Dr. Charu Wadhwa	Orthopedics
229	Dr. Sameer Qureshi	Dermatology
230	Dr. Ekta Bhalla	Pediatrics
231	Dr. Dinesh Tripathi	Gynecology
232	Dr. Aparna Mukherjee	Ophthalmology
233	Dr. Manish Talwar	Psychiatry
234	Dr. Sonal Aggarwal	ENT
235	Dr. Harshit Vohra	General Medicine
236	Dr. Rachna Iqbal	Emergency Medicine
237	Dr. Tejas Kulkarni	Oncology
238	Dr. Nandini Roy	Nephrology
239	Dr. Pratik Goyal	Gastroenterology
240	Dr. Alka Bhatnagar	Pulmonology
241	Dr. Shashank Tyagi	Endocrinology
242	Dr. Mitali Fernandes	Urology
243	Dr. Yuvraj Sinha	Radiology
244	Dr. Swarnima Kaul	Anesthesiology
245	Dr. Anirudh Batra	Pathology
246	Dr. Reema Thomas	Infectious Diseases
247	Dr. Jayant Bhatt	Rheumatology
248	Dr. Sonali Chatterjee	Plastic Surgery
249	Dr. Faisal Mirza	General Surgery
250	Dr. Trisha D'Souza	Family Medicine
\.


--
-- Data for Name: doctors_import; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.doctors_import (doctor_id, name, specialization) FROM stdin;
201	Dr. Vikram Chawla	Cardiology
202	Dr. Neelam Saxena	Neurology
203	Dr. Karan Oberoi	Orthopedics
204	Dr. Shalini Arora	Dermatology
205	Dr. Abhishek Tandon	Pediatrics
206	Dr. Pankaj Bansal	Gynecology
207	Dr. Ritu Mahajan	Ophthalmology
208	Dr. Mohit Suri	Psychiatry
209	Dr. Deepika Menon	ENT
210	Dr. Tarun Bhardwaj	General Medicine
211	Dr. Nitin Ahuja	Emergency Medicine
212	Dr. Swati Ghosh	Oncology
213	Dr. Akash Dutta	Nephrology
214	Dr. Monica Bedi	Gastroenterology
215	Dr. Chirag Anand	Pulmonology
216	Dr. Pallavi Desai	Endocrinology
217	Dr. Saurabh Saxena	Urology
218	Dr. Komal Sood	Radiology
219	Dr. Abhinav Kohli	Anesthesiology
220	Dr. Bhavna Arora	Pathology
221	Dr. Rajeev Puri	Infectious Diseases
222	Dr. Shweta Kulkarni	Rheumatology
223	Dr. Amitabh Sen	Plastic Surgery
224	Dr. Garima Sethi	General Surgery
225	Dr. Keshav Prasad	Family Medicine
226	Dr. Namrata Pillai	Cardiology
227	Dr. Lokesh Yadav	Neurology
228	Dr. Charu Wadhwa	Orthopedics
229	Dr. Sameer Qureshi	Dermatology
230	Dr. Ekta Bhalla	Pediatrics
231	Dr. Dinesh Tripathi	Gynecology
232	Dr. Aparna Mukherjee	Ophthalmology
233	Dr. Manish Talwar	Psychiatry
234	Dr. Sonal Aggarwal	ENT
235	Dr. Harshit Vohra	General Medicine
236	Dr. Rachna Iqbal	Emergency Medicine
237	Dr. Tejas Kulkarni	Oncology
238	Dr. Nandini Roy	Nephrology
239	Dr. Pratik Goyal	Gastroenterology
240	Dr. Alka Bhatnagar	Pulmonology
241	Dr. Shashank Tyagi	Endocrinology
242	Dr. Mitali Fernandes	Urology
243	Dr. Yuvraj Sinha	Radiology
244	Dr. Swarnima Kaul	Anesthesiology
245	Dr. Anirudh Batra	Pathology
246	Dr. Reema Thomas	Infectious Diseases
247	Dr. Jayant Bhatt	Rheumatology
248	Dr. Sonali Chatterjee	Plastic Surgery
249	Dr. Faisal Mirza	General Surgery
250	Dr. Trisha D'Souza	Family Medicine
\.


--
-- Data for Name: emergency; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.emergency (emergency_id, patient_id, priority) FROM stdin;
\.


--
-- Data for Name: emergency_cases; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.emergency_cases (emergency_id, patient_id, priority) FROM stdin;
\.


--
-- Data for Name: medical_records; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.medical_records (record_id, patient_id, disease, treatment, medicine) FROM stdin;
1	1001	Asthma	Admit for 5 weeks	
\.


--
-- Data for Name: patients; Type: TABLE DATA; Schema: public; Owner: -
--

COPY public.patients (patient_id, name, age, gender) FROM stdin;
1001	Ansh Rana	62	F
1002	Sahil Sharma	32	F
1003	Ansh Kapoor	82	F
1004	Neha Bisht	35	M
1005	Saanvi Rana	26	F
1006	Kavya Singh	52	M
1007	Saanvi Negi	31	M
1008	Vivaan Saxena	32	F
1009	Zoya Nair	44	F
1010	Kabir Khan	68	M
1011	Myra Verma	25	F
1012	Nikhil Nair	24	F
1013	Aditya Thakur	18	M
1014	Kavya Chauhan	55	M
1015	Rohan Rana	64	F
1016	Tara Bisht	30	F
1017	Ishaan Nair	64	F
1018	Riya Chauhan	36	F
1019	Aarav Rawat	55	F
1020	Reyansh Joshi	36	F
1021	Diya Bisht	32	M
1022	Arjun Nair	68	F
1023	Ansh Singh	53	F
1024	Kabir Negi	39	M
1025	Yash Singh	24	M
1026	Kunal Pant	19	F
1027	Myra Rana	56	M
1028	Kabir Bhatia	21	M
1029	Kavya Bisht	72	M
1030	Sara Mehta	77	F
1031	Saanvi Nair	39	F
1032	Zoya Gupta	53	M
1033	Ishaan Bhatia	21	F
1034	Neha Khan	52	F
1035	Pihu Das	35	M
1036	Tara Bhatia	51	M
1037	Zoya Das	74	F
1038	Anaya Chauhan	57	F
1039	Kiara Nair	21	F
1040	Vivaan Rana	38	M
1041	Dev Pant	59	M
1042	Riya Thakur	59	F
1043	Meera Bisht	67	M
1044	Zoya Saxena	27	F
1045	Arjun Rawat	34	M
1046	Avni Sharma	74	M
1047	Meera Saxena	57	F
1048	Reyansh Nair	43	M
1049	Ishaan Chauhan	30	F
1050	Myra Sharma	68	M
1051	Sara Rawat	53	F
1052	Dev Nair	56	M
1053	Dev Bisht	49	F
1054	Rohan Khan	47	F
1055	Saanvi Chauhan	19	F
1056	Riya Nair	24	F
1057	Riya Malik	62	F
1058	Riya Thakur	79	M
1059	Sara Singh	51	F
1060	Tara Rawat	22	F
1061	Nikhil Kapoor	35	F
1062	Diya Thakur	69	F
1063	Ansh Rana	43	F
1064	Sahil Malik	56	F
1065	Aanya Verma	77	M
1066	Rohan Pant	63	M
1067	Kavya Khan	48	M
1068	Yash Chauhan	78	M
1069	Kiara Verma	51	F
1070	Aanya Das	19	M
1071	Isha Sharma	62	F
1072	Reyansh Negi	49	F
1073	Pihu Chauhan	49	M
1074	Aanya Chauhan	51	F
1075	Ishaan Singh	84	F
1076	Sahil Joshi	22	F
1077	Riya Gupta	24	M
1078	Ishaan Gupta	75	M
1079	Tara Gupta	34	F
1080	Dev Sharma	77	M
1081	Riya Rawat	36	F
1082	Saanvi Bhatia	83	M
1083	Yash Singh	72	M
1084	Pihu Mehta	51	M
1085	Ansh Khan	43	F
1086	Aanya Saxena	79	F
1087	Dev Nair	32	M
1088	Aditya Rana	60	M
1089	Rohan Nair	43	F
1090	Myra Khan	31	M
1091	Saanvi Bisht	79	M
1092	Yash Nair	31	F
1093	Riya Thakur	40	F
1094	Isha Chauhan	33	F
1095	Meera Mehta	21	M
1096	Isha Kapoor	73	M
1097	Riya Pant	85	M
1098	Kunal Verma	55	M
1099	Vivaan Rana	36	F
1100	Neha Verma	24	M
1101	Aarav Chauhan	76	M
1102	Myra Rana	80	M
1103	Ansh Saxena	32	M
1104	Saanvi Malik	51	F
1105	Ansh Das	58	M
1106	Dev Khan	23	M
1107	Aanya Singh	54	F
1108	Saanvi Bisht	36	M
1109	Zoya Thakur	29	M
1110	Aanya Joshi	20	M
1111	Aanya Mehta	51	M
1112	Yash Das	77	F
1113	Kunal Khan	35	F
1114	Neha Thakur	36	M
1115	Arjun Kapoor	33	M
1116	Sahil Mehta	81	F
1117	Isha Joshi	40	F
1118	Riya Chauhan	66	M
1119	Anaya Rana	42	M
1120	Neha Khan	56	F
1121	Riya Gupta	71	F
1122	Meera Nair	34	F
1123	Diya Negi	60	M
1124	Zoya Singh	18	F
1125	Vivaan Verma	45	F
1126	Tara Negi	77	M
1127	Arjun Gupta	38	F
1128	Dev Kapoor	52	F
1129	Pihu Gupta	33	M
1130	Aditya Bhatia	84	M
1131	Sara Rana	42	F
1132	Vivaan Saxena	52	F
1133	Tara Verma	54	M
1134	Pihu Rana	55	F
1135	Sara Rawat	58	M
1136	Ishaan Verma	73	F
1137	Reyansh Thakur	60	F
1138	Pihu Das	38	F
1139	Aanya Verma	43	F
1140	Reyansh Negi	53	M
1141	Pihu Pant	41	F
1142	Dev Nair	39	M
1143	Myra Thakur	63	M
1144	Meera Das	71	M
1145	Tara Rana	29	M
1146	Dev Rana	48	F
1147	Reyansh Malik	80	F
1148	Saanvi Bisht	84	F
1149	Riya Das	56	F
1150	Vivaan Khan	51	M
1151	Reyansh Mehta	52	M
1152	Nikhil Singh	71	F
1153	Tara Nair	53	F
1154	Kiara Rawat	79	F
1155	Aarav Nair	39	F
1156	Zoya Sharma	83	M
1157	Yash Negi	64	F
1158	Arjun Rana	32	M
1159	Yash Khan	18	F
1160	Avni Verma	76	F
1161	Meera Singh	57	F
1162	Rohan Saxena	60	M
1163	Pihu Negi	53	F
1164	Kabir Mehta	49	F
1165	Sara Sharma	37	M
1166	Aditya Verma	34	F
1167	Vivaan Singh	75	F
1168	Kabir Pant	37	F
1169	Sara Khan	70	F
1170	Riya Malik	21	F
1171	Diya Thakur	74	F
1172	Pihu Chauhan	35	M
1173	Meera Bisht	83	M
1174	Sahil Nair	66	M
1175	Avni Negi	21	M
1176	Diya Verma	41	F
1177	Arjun Pant	75	M
1178	Nikhil Gupta	41	F
1179	Kavya Gupta	51	M
1180	Kavya Bisht	51	F
1181	Diya Mehta	78	F
1182	Saanvi Kapoor	50	M
1183	Diya Sharma	39	F
1184	Avni Chauhan	58	F
1185	Kunal Negi	82	M
1186	Dev Mehta	34	M
1187	Ishaan Malik	19	M
1188	Kiara Mehta	81	F
1189	Myra Pant	66	F
1190	Tara Bisht	35	F
1191	Pihu Thakur	50	M
1192	Vivaan Bhatia	51	F
1193	Aanya Mehta	71	M
1194	Aarav Thakur	47	F
1195	Ansh Negi	85	M
1196	Isha Bisht	84	F
1197	Pihu Khan	81	M
1198	Ansh Joshi	72	M
1199	Dev Joshi	70	M
1200	Sara Saxena	33	F
\.


--
-- Name: appointments_appointment_id_seq; Type: SEQUENCE SET; Schema: public; Owner: -
--

SELECT pg_catalog.setval('public.appointments_appointment_id_seq', 1, false);


--
-- Name: emergency_cases_emergency_id_seq; Type: SEQUENCE SET; Schema: public; Owner: -
--

SELECT pg_catalog.setval('public.emergency_cases_emergency_id_seq', 1, false);


--
-- Name: emergency_emergency_id_seq; Type: SEQUENCE SET; Schema: public; Owner: -
--

SELECT pg_catalog.setval('public.emergency_emergency_id_seq', 1, false);


--
-- Name: medical_records_record_id_seq; Type: SEQUENCE SET; Schema: public; Owner: -
--

SELECT pg_catalog.setval('public.medical_records_record_id_seq', 1, true);


--
-- Name: appointments appointments_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.appointments
    ADD CONSTRAINT appointments_pkey PRIMARY KEY (appointment_id);


--
-- Name: doctors doctors_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.doctors
    ADD CONSTRAINT doctors_pkey PRIMARY KEY (doctor_id);


--
-- Name: emergency_cases emergency_cases_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.emergency_cases
    ADD CONSTRAINT emergency_cases_pkey PRIMARY KEY (emergency_id);


--
-- Name: emergency emergency_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.emergency
    ADD CONSTRAINT emergency_pkey PRIMARY KEY (emergency_id);


--
-- Name: medical_records medical_records_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.medical_records
    ADD CONSTRAINT medical_records_pkey PRIMARY KEY (record_id);


--
-- Name: patients patients_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.patients
    ADD CONSTRAINT patients_pkey PRIMARY KEY (patient_id);


--
-- Name: appointments appointments_doctor_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.appointments
    ADD CONSTRAINT appointments_doctor_id_fkey FOREIGN KEY (doctor_id) REFERENCES public.doctors(doctor_id);


--
-- Name: appointments appointments_patient_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.appointments
    ADD CONSTRAINT appointments_patient_id_fkey FOREIGN KEY (patient_id) REFERENCES public.patients(patient_id);


--
-- Name: emergency_cases emergency_cases_patient_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.emergency_cases
    ADD CONSTRAINT emergency_cases_patient_id_fkey FOREIGN KEY (patient_id) REFERENCES public.patients(patient_id);


--
-- Name: medical_records medical_records_patient_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.medical_records
    ADD CONSTRAINT medical_records_patient_id_fkey FOREIGN KEY (patient_id) REFERENCES public.patients(patient_id);


--
-- PostgreSQL database dump complete
--

\unrestrict mqhfyzT1iUKkHRwfNpPww4w7brcS680LTU6Asd3zyCt7M93Pf7QuwDkHfQduuow

