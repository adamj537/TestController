#!/bin/bash

# FW-1 TC Firmware Test Report Generator
# Generates beautiful HTML test reports with charts and history

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPORTS_DIR="$PROJECT_DIR/test_reports"
HTML_FILE="$REPORTS_DIR/index.html"
TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
COMPILER="${1:-gcc}"

# Create reports directory
mkdir -p "$REPORTS_DIR"

# Function to count test results from output
count_tests() {
    local file=$1
    if [ -f "$file" ]; then
        grep -o "[0-9]* Tests [0-9]* Failures" "$file" | head -1
    else
        echo "0 Tests 0 Failures"
    fi
}

# Collect test results
echo "Collecting test results from compiled tests..."

cd "$PROJECT_DIR"

# Run all tests if not already done
if [ ! -f "$PROJECT_DIR/test_summary.txt" ]; then
    echo "Running tests..."

    # Phase 1
    gcc -I. -DDEBUG test/test_hal_gpio.c hal/mock/hal_gpio_mock.c -o test_hal_gpio 2>/dev/null
    ./test_hal_gpio > phase1.txt 2>&1 || true

    # Phase 2
    gcc -I. -DDEBUG test/test_hardware.c hal/mock/hal_*.c bsp/bsp_g3_tc.c -o test_hardware 2>/dev/null
    ./test_hardware > phase2.txt 2>&1 || true

    # Phase 3
    gcc -I. -DDEBUG test/test_recipe_manager.c storage/storage_mock.c recipes/recipe_manager_mock.c -o test_recipes 2>/dev/null
    ./test_recipes > phase3.txt 2>&1 || true

    # Phase 4
    gcc -I. -DDEBUG test/test_dut_interface.c hal/mock/hal_uart_mock.c dut/dut_interface_mock.c -o test_dut 2>/dev/null
    ./test_dut > phase4.txt 2>&1 || true
fi

# Parse results
PHASE1=$(count_tests phase1.txt)
PHASE2=$(count_tests phase2.txt)
PHASE3=$(count_tests phase3.txt)
PHASE4=$(count_tests phase4.txt)

# Extract numbers
PHASE1_TESTS=$(echo $PHASE1 | awk '{print $1}')
PHASE1_FAILS=$(echo $PHASE1 | awk '{print $3}')
PHASE2_TESTS=$(echo $PHASE2 | awk '{print $1}')
PHASE2_FAILS=$(echo $PHASE2 | awk '{print $3}')
PHASE3_TESTS=$(echo $PHASE3 | awk '{print $1}')
PHASE3_FAILS=$(echo $PHASE3 | awk '{print $3}')
PHASE4_TESTS=$(echo $PHASE4 | awk '{print $1}')
PHASE4_FAILS=$(echo $PHASE4 | awk '{print $3}')

TOTAL_TESTS=$((PHASE1_TESTS + PHASE2_TESTS + PHASE3_TESTS + PHASE4_TESTS))
TOTAL_FAILS=$((PHASE1_FAILS + PHASE2_FAILS + PHASE3_FAILS + PHASE4_FAILS))
TOTAL_PASS=$((TOTAL_TESTS - TOTAL_FAILS))
SUCCESS_RATE=$(( (TOTAL_PASS * 100) / TOTAL_TESTS ))

# Determine status color
if [ $TOTAL_FAILS -eq 0 ]; then
    STATUS_COLOR="#28a745"  # Green
    STATUS_TEXT="PASSING"
else
    STATUS_COLOR="#dc3545"  # Red
    STATUS_TEXT="FAILING"
fi

# Generate HTML report
cat > "$HTML_FILE" << 'EOF'
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>FW-1 TC Firmware Test Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }

        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            border-radius: 12px;
            box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
            overflow: hidden;
        }

        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 40px 20px;
            text-align: center;
        }

        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
        }

        .header p {
            font-size: 1.1em;
            opacity: 0.9;
        }

        .status-badge {
            display: inline-block;
            padding: 10px 20px;
            border-radius: 20px;
            background: rgba(255, 255, 255, 0.2);
            margin-top: 10px;
            font-weight: bold;
            font-size: 1.2em;
        }

        .content {
            padding: 40px;
        }

        .summary {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            margin-bottom: 40px;
        }

        .summary-card {
            background: #f8f9fa;
            border-left: 4px solid #667eea;
            padding: 20px;
            border-radius: 8px;
            text-align: center;
        }

        .summary-card .value {
            font-size: 2.5em;
            font-weight: bold;
            color: #667eea;
        }

        .summary-card .label {
            color: #666;
            margin-top: 5px;
        }

        .success { border-left-color: #28a745; }
        .success .value { color: #28a745; }

        .failure { border-left-color: #dc3545; }
        .failure .value { color: #dc3545; }

        .charts {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(400px, 1fr));
            gap: 30px;
            margin-bottom: 40px;
        }

        .chart-container {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
        }

        .chart-container h3 {
            margin-bottom: 20px;
            color: #333;
        }

        .phases {
            margin-bottom: 40px;
        }

        .phases h2 {
            margin-bottom: 20px;
            color: #333;
            border-bottom: 2px solid #667eea;
            padding-bottom: 10px;
        }

        .phase {
            background: #f8f9fa;
            border-left: 4px solid #667eea;
            padding: 20px;
            margin-bottom: 15px;
            border-radius: 8px;
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
        }

        .phase-info h3 {
            margin-bottom: 10px;
            color: #333;
        }

        .phase-stats {
            display: flex;
            gap: 20px;
        }

        .phase-stat {
            text-align: center;
        }

        .phase-stat .number {
            font-size: 1.8em;
            font-weight: bold;
            color: #667eea;
        }

        .phase-stat .label {
            color: #666;
            font-size: 0.9em;
        }

        .progress-bar {
            width: 100%;
            height: 8px;
            background: #e9ecef;
            border-radius: 4px;
            overflow: hidden;
            margin-top: 10px;
        }

        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, #28a745, #20c997);
            transition: width 0.3s ease;
        }

        .footer {
            background: #f8f9fa;
            padding: 20px;
            text-align: center;
            color: #666;
            font-size: 0.9em;
            border-top: 1px solid #dee2e6;
        }

        .timestamp {
            color: #999;
            font-size: 0.85em;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🚀 FW-1 TC Firmware Test Report</h1>
            <p>Embedded Tester Client - Unit Test Results</p>
            <div class="status-badge" style="background: STATUSCOLOR;">STATUS_TEXT</div>
        </div>

        <div class="content">
            <!-- Summary Cards -->
            <div class="summary">
                <div class="summary-card success">
                    <div class="value">PASS_COUNT</div>
                    <div class="label">Tests Passed</div>
                </div>
                <div class="summary-card failure">
                    <div class="value">FAIL_COUNT</div>
                    <div class="label">Tests Failed</div>
                </div>
                <div class="summary-card">
                    <div class="value">TOTAL_COUNT</div>
                    <div class="label">Total Tests</div>
                </div>
                <div class="summary-card">
                    <div class="value">SUCCESS_PCT%</div>
                    <div class="label">Success Rate</div>
                </div>
            </div>

            <!-- Charts -->
            <div class="charts">
                <div class="chart-container">
                    <h3>Test Results Distribution</h3>
                    <canvas id="resultsChart"></canvas>
                </div>
                <div class="chart-container">
                    <h3>Tests by Phase</h3>
                    <canvas id="phasesChart"></canvas>
                </div>
            </div>

            <!-- Phase Results -->
            <div class="phases">
                <h2>📊 Detailed Phase Results</h2>

                <div class="phase">
                    <div class="phase-info">
                        <h3>Phase 1: HAL/BSP Foundation</h3>
                        <p>Hardware abstraction layer and board support package</p>
                    </div>
                    <div class="phase-stats">
                        <div class="phase-stat">
                            <div class="number">PHASE1_TESTS</div>
                            <div class="label">Tests</div>
                        </div>
                        <div class="phase-stat">
                            <div class="number">PHASE1_FAILS</div>
                            <div class="label">Failures</div>
                        </div>
                    </div>
                </div>

                <div class="phase">
                    <div class="phase-info">
                        <h3>Phase 2: Hardware Module</h3>
                        <p>Hardware abstraction and initialization</p>
                    </div>
                    <div class="phase-stats">
                        <div class="phase-stat">
                            <div class="number">PHASE2_TESTS</div>
                            <div class="label">Tests</div>
                        </div>
                        <div class="phase-stat">
                            <div class="number">PHASE2_FAILS</div>
                            <div class="label">Failures</div>
                        </div>
                    </div>
                </div>

                <div class="phase">
                    <div class="phase-info">
                        <h3>Phase 3: Recipe Manager & Storage</h3>
                        <p>Test recipe management and persistence</p>
                    </div>
                    <div class="phase-stats">
                        <div class="phase-stat">
                            <div class="number">PHASE3_TESTS</div>
                            <div class="label">Tests</div>
                        </div>
                        <div class="phase-stat">
                            <div class="number">PHASE3_FAILS</div>
                            <div class="label">Failures</div>
                        </div>
                    </div>
                </div>

                <div class="phase">
                    <div class="phase-info">
                        <h3>Phase 4: DUT Interface</h3>
                        <p>FW-2 UART protocol bridge</p>
                    </div>
                    <div class="phase-stats">
                        <div class="phase-stat">
                            <div class="number">PHASE4_TESTS</div>
                            <div class="label">Tests</div>
                        </div>
                        <div class="phase-stat">
                            <div class="number">PHASE4_FAILS</div>
                            <div class="label">Failures</div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div class="footer">
            <p>FW-1 TC Firmware - Embedded Tester Client</p>
            <p class="timestamp">Generated: TIMESTAMP | Compiler: COMPILER</p>
        </div>
    </div>

    <script>
        // Results pie chart
        const resultsCtx = document.getElementById('resultsChart').getContext('2d');
        new Chart(resultsCtx, {
            type: 'doughnut',
            data: {
                labels: ['Passed', 'Failed'],
                datasets: [{
                    data: [PASS_COUNT, FAIL_COUNT],
                    backgroundColor: ['#28a745', '#dc3545'],
                    borderColor: ['#20c997', '#c82333'],
                    borderWidth: 2
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: true,
                plugins: {
                    legend: {
                        position: 'bottom'
                    }
                }
            }
        });

        // Phases bar chart
        const phasesCtx = document.getElementById('phasesChart').getContext('2d');
        new Chart(phasesCtx, {
            type: 'bar',
            data: {
                labels: ['Phase 1\nHAL/BSP', 'Phase 2\nHardware', 'Phase 3\nRecipes', 'Phase 4\nDUT IF'],
                datasets: [{
                    label: 'Tests',
                    data: [PHASE1_TESTS, PHASE2_TESTS, PHASE3_TESTS, PHASE4_TESTS],
                    backgroundColor: '#667eea',
                    borderColor: '#5568d3',
                    borderWidth: 1
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: true,
                indexAxis: 'x',
                plugins: {
                    legend: {
                        display: false
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        max: 30
                    }
                }
            }
        });
    </script>
</body>
</html>
EOF

# Replace placeholders
sed -i "s/PASS_COUNT/$TOTAL_PASS/g" "$HTML_FILE"
sed -i "s/FAIL_COUNT/$TOTAL_FAILS/g" "$HTML_FILE"
sed -i "s/TOTAL_COUNT/$TOTAL_TESTS/g" "$HTML_FILE"
sed -i "s/SUCCESS_PCT/$SUCCESS_RATE/g" "$HTML_FILE"
sed -i "s/STATUS_TEXT/$STATUS_TEXT/g" "$HTML_FILE"
sed -i "s|STATUSCOLOR|$STATUS_COLOR|g" "$HTML_FILE"
sed -i "s/PHASE1_TESTS/$PHASE1_TESTS/g" "$HTML_FILE"
sed -i "s/PHASE1_FAILS/$PHASE1_FAILS/g" "$HTML_FILE"
sed -i "s/PHASE2_TESTS/$PHASE2_TESTS/g" "$HTML_FILE"
sed -i "s/PHASE2_FAILS/$PHASE2_FAILS/g" "$HTML_FILE"
sed -i "s/PHASE3_TESTS/$PHASE3_TESTS/g" "$HTML_FILE"
sed -i "s/PHASE3_FAILS/$PHASE3_FAILS/g" "$HTML_FILE"
sed -i "s/PHASE4_TESTS/$PHASE4_TESTS/g" "$HTML_FILE"
sed -i "s/PHASE4_FAILS/$PHASE4_FAILS/g" "$HTML_FILE"
sed -i "s/TIMESTAMP/$TIMESTAMP/g" "$HTML_FILE"
sed -i "s/COMPILER/$COMPILER/g" "$HTML_FILE"

echo ""
echo "✓ Test report generated: $HTML_FILE"
echo ""
echo "Summary:"
echo "  Total Tests: $TOTAL_TESTS"
echo "  Passed: $TOTAL_PASS"
echo "  Failed: $TOTAL_FAILS"
echo "  Success Rate: $SUCCESS_RATE%"
echo ""
echo "To view the report:"
echo "  open $HTML_FILE"
