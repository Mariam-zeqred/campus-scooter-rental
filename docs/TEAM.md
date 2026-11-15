# Team and credits

This was a team graduation project in the Communications and Computer Engineering programme, Faculty of Engineering, Mansoura University. It is documented in two project books; the lists below are copied from their title pages. Eight students worked on the project in the first semester and four of them continued in the second semester, when the remaining work needed a smaller team.

## First book (2025): "Smart E-Scooter Rental System for University Campus"

Covers the app design, the web front end, the back-end API, the ID-verification script and the AWS cloud work.

Students: Ola Saleh Ibrahim El-Dosouky, Mariam M. Mamdouh, Abdelrahman Wael Abdelhady, Abdelrahman Mahmoud Hussein, Abdelrahman M. Mahmoud, Andrew Hisham Osama, Ahmed Abdelfattah El-Mekawey, Mark Maged Edward.

Supervisors: Prof. Dr. Ahmed I. Saleh, Dr. Mohamed S. Saraya, Dr. Ahmed N. Sheta, Eng. Mohamed M. Mowafy.

## Final book (2025/2026): "Smart Electric Scooter Rental System for Enclosed Areas"

Adds the mechanical model, the hardware, the embedded firmware and the IoT connectivity chapters.

Students: Ola Saleh Ibrahim ElDosouky, Abdelrahman Wael Alsharkawi, Mariam Mohamed Mamdouh, Abdelrahman Mahmoud Hussien.

Supervisors: Prof. Dr. Ahmed I. Saleh, Prof. Dr. Mostafa El Boz, Eng. Mohamed Salah Waheeb.

## Who did what

The books do not attribute chapters to individual students. The division below is the repository author's own account.

| Part | In the books | In this repository |
|---|---|---|
| Cloud infrastructure on AWS | First book, ch. 21 | Rewritten as code in `cloud/` by Mariam Loutfi (full name Mariam Mohamed Mamdouh Loutfi; the books print it without the family name), who did the original cloud work and led the team. |
| Hardware and embedded firmware | Final book, ch. 7 to 9 | Rewritten in `firmware/` by the same author, from her surviving prototype sketch. |
| App design (Figma) | First book, ch. 1 to 5 | Team members' work. Shown in pictures only. |
| Web front end (React, Tailwind, Vite, Framer Motion) | First book, ch. 6 to 14 | Team members' work. Described only. |
| Back-end API (Node.js, Express, MongoDB) | First book, ch. 15 to 19 | Team members' work. Its data model and ride lifecycle are summarised in `docs/DATA_MODEL.md` and re-expressed on DynamoDB in `cloud/lambdas/rides_api/`. |
| ID verification from the card barcode (Python) | First book, ch. 20 | Team members' work. The field patterns in `cloud/lambdas/id_processor/id_fields.py` are the ones printed in the book. |
| Mechanical model | Final book, ch. 6 | Team's work. One CAD picture is shown in `docs/HARDWARE.md`. |

## Pictures

Everything under `docs/img/` was taken from the two books:

- `app-*.png`: screens of the "Scoot me!" app design (first book, ch. 1 and 5).
- `aws-phase1-*.png`, `aws-phase2-*.png`: architecture diagrams (first book, ch. 21).
- `aws-console-*.png`: AWS console screenshots from 2025 (first book, ch. 21), cropped to remove the account menu.
- `book-fig33-*.png`, `book-fig35-*.png`, `book-fig36-*.png`: figures 33, 35 and 36 of the final book.

Pictures from the books that showed an IP address, a domain's DNS records, account or resource identifiers, or an ID card were left out. The university and programme logos were also left out: this repository is not an official university publication.
